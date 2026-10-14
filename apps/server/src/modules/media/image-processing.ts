import { createHash } from 'node:crypto';
import sharp, { type Metadata, type OutputInfo } from 'sharp';
import { MEDIA_LIMITS, MEDIA_SPECS, type MediaKind } from '@akeno/shared';
import { AppError } from '../../utils/errors.js';

/** Keep libvips small and predictable on a 1-2 GB VPS: no operation cache, one thread per image. */
sharp.cache(false);
sharp.concurrency(1);

export type SniffedType = 'jpeg' | 'png' | 'webp' | 'gif';

/**
 * Detects the image type from its first bytes. File names and client-supplied MIME types are
 * never trusted; anything that is not one of these four signatures (SVG, HTML, PDF, TIFF,
 * executables, ...) is rejected before it reaches the decoder.
 */
export function sniffImageType(data: Buffer): SniffedType | null {
  if (data.length < 12) return null;
  if (data[0] === 0xff && data[1] === 0xd8 && data[2] === 0xff) return 'jpeg';
  if (data.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))) return 'png';
  if (data.toString('latin1', 0, 4) === 'RIFF' && data.toString('latin1', 8, 12) === 'WEBP') return 'webp';
  const gif = data.toString('latin1', 0, 6);
  if (gif === 'GIF87a' || gif === 'GIF89a') return 'gif';
  return null;
}

export interface ProcessedVariant {
  file: string;
  width: number;
  height: number;
  data: Buffer;
}

export interface ProcessedImage {
  /** Size of the cropped, oriented source. */
  width: number;
  height: number;
  sourceSha256: string;
  variants: ProcessedVariant[];
}

const rejected = (message: string) => new AppError(422, 'INVALID_IMAGE', message);

/** Largest centered region with the requested aspect ratio. */
export function centerCrop(width: number, height: number, aspect: { width: number; height: number }) {
  const target = aspect.width / aspect.height;
  if (width / height > target) {
    const cropWidth = Math.max(1, Math.round(height * target));
    return { left: Math.floor((width - cropWidth) / 2), top: 0, width: cropWidth, height };
  }
  const cropHeight = Math.max(1, Math.round(width / target));
  return { left: 0, top: Math.floor((height - cropHeight) / 2), width, height: cropHeight };
}

/**
 * Validates and re-encodes an uploaded image into the fixed variants of its kind.
 *
 * - The type is detected from the contents and must agree with what the decoder finds.
 * - Dimensions and pixel count are checked from the header before decoding the pixels, and the
 *   decoder itself is limited (limitInputPixels), so decompression bombs are refused cheaply.
 * - Only the first frame is used (animated images become still images).
 * - EXIF orientation is applied, then every piece of metadata (EXIF, GPS, XMP, ICC, comments) is
 *   dropped by re-encoding to sRGB WebP/JPEG without metadata.
 * - Images are center-cropped to the kind's aspect ratio and never enlarged.
 */
export async function processImage(kind: MediaKind, data: Buffer): Promise<ProcessedImage> {
  if (data.length === 0) throw rejected('The file is empty.');
  if (data.length > MEDIA_LIMITS.uploadMaxBytes) {
    throw new AppError(413, 'PAYLOAD_TOO_LARGE', `Images can be at most ${MEDIA_LIMITS.uploadMaxBytes / (1024 * 1024)} MB.`);
  }
  const sniffed = sniffImageType(data);
  if (!sniffed) throw rejected('Use a JPEG, PNG, WebP or GIF image.');

  const spec = MEDIA_SPECS[kind];
  const decoderOptions = {
    limitInputPixels: MEDIA_LIMITS.inputMaxPixels,
    failOn: 'error' as const,
    sequentialRead: true,
    animated: false,
    pages: 1,
  };

  let metadata: Metadata;
  try {
    metadata = await sharp(data, decoderOptions).metadata();
  } catch {
    throw rejected('The image could not be read. It may be damaged or in an unsupported format.');
  }
  if (metadata.format !== sniffed) throw rejected('The file contents do not match a supported image format.');
  const rawWidth = metadata.width ?? 0;
  const rawHeight = metadata.height ?? 0;
  if (rawWidth < 1 || rawHeight < 1) throw rejected('The image has no pixels.');
  if (rawWidth > MEDIA_LIMITS.inputMaxDimension || rawHeight > MEDIA_LIMITS.inputMaxDimension) {
    throw rejected(`Images can be at most ${MEDIA_LIMITS.inputMaxDimension} pixels wide or high.`);
  }
  if (rawWidth * rawHeight > MEDIA_LIMITS.inputMaxPixels) throw rejected('The image has too many pixels.');

  // EXIF orientations 5-8 swap width and height.
  const swapped = (metadata.orientation ?? 1) >= 5;
  const width = swapped ? rawHeight : rawWidth;
  const height = swapped ? rawWidth : rawHeight;
  const crop = centerCrop(width, height, spec.aspect);
  if (crop.width < spec.minWidth || crop.height < spec.minHeight) {
    throw rejected(`The image is too small. Use at least ${spec.minWidth} × ${spec.minHeight} pixels.`);
  }

  let base: Buffer;
  let baseInfo: OutputInfo;
  try {
    // Decode once: orient, crop, scale down to the largest variant and normalize to 8-bit sRGB.
    // The (bounded) intermediate stays uncompressed in memory for the variant encoders.
    const largest = spec.variants.reduce((max, variant) => (variant.width > max.width ? variant : max));
    const result = await sharp(data, decoderOptions)
      .rotate()
      .extract(crop)
      .resize({ width: largest.width, height: largest.height, fit: 'inside', withoutEnlargement: true, kernel: 'lanczos3' })
      .toColourspace('srgb')
      .timeout({ seconds: 20 })
      .raw()
      .toBuffer({ resolveWithObject: true });
    base = result.data;
    baseInfo = result.info;
  } catch {
    throw rejected('The image could not be processed. It may be damaged.');
  }
  const raw = { width: baseInfo.width, height: baseInfo.height, channels: baseInfo.channels };

  const variants: ProcessedVariant[] = [];
  for (const variant of spec.variants) {
    let pipeline = sharp(base, { raw }).resize({
      width: variant.width,
      height: variant.height,
      fit: 'inside',
      withoutEnlargement: true,
      kernel: 'lanczos3',
    });
    pipeline =
      variant.format === 'webp'
        ? pipeline.webp({ quality: kind === 'avatar' || kind === 'game_icon' ? 86 : 82, effort: 4, smartSubsample: true })
        : // JPEG has no alpha channel: transparent areas become the app's dark surface colour.
          pipeline.flatten({ background: '#0d1323' }).jpeg({ quality: 84, mozjpeg: true, chromaSubsampling: '4:2:0' });
    const { data: encoded, info } = await pipeline.timeout({ seconds: 20 }).toBuffer({ resolveWithObject: true });
    variants.push({ file: variant.file, width: info.width, height: info.height, data: encoded });
  }

  return {
    width: crop.width,
    height: crop.height,
    sourceSha256: createHash('sha256').update(data).digest('hex'),
    variants,
  };
}

/** Limits how many images are decoded at once (memory bound on small servers). */
export class Semaphore {
  private active = 0;
  private readonly waiting: Array<() => void> = [];

  constructor(
    private readonly limit: number,
    private readonly maxQueue = 20,
  ) {}

  async run<T>(work: () => Promise<T>): Promise<T> {
    if (this.active >= this.limit) {
      if (this.waiting.length >= this.maxQueue) {
        throw new AppError(503, 'MEDIA_BUSY', 'The server is busy processing images. Please try again in a moment.');
      }
      // The slot is handed over directly by the finishing job (see finally), so the limit holds.
      await new Promise<void>((resolve) => this.waiting.push(resolve));
    } else {
      this.active += 1;
    }
    try {
      return await work();
    } finally {
      const next = this.waiting.shift();
      if (next) next();
      else this.active -= 1;
    }
  }
}
