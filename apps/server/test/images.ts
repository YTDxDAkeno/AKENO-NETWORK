import sharp from 'sharp';

/** Generates real test images with sharp (no binary fixtures in the repository). */
export async function testImage(
  width: number,
  height: number,
  format: 'png' | 'jpeg' | 'webp' | 'gif' = 'png',
  options: { color?: string; exif?: Record<string, Record<string, string>>; orientation?: number } = {},
): Promise<Buffer> {
  let image = sharp({ create: { width, height, channels: 3, background: options.color ?? '#3366cc' } });
  // A diagonal band so crops and rotations produce different pixels.
  const band = Buffer.from(
    `<svg width="${width}" height="${height}"><rect x="0" y="0" width="${Math.max(1, Math.floor(width / 4))}" height="${height}" fill="#ff3355"/></svg>`,
  );
  image = image.composite([{ input: band, top: 0, left: 0 }]);
  if (options.exif) image = image.withExif(options.exif);
  if (options.orientation) image = image.withMetadata({ orientation: options.orientation });
  switch (format) {
    case 'png':
      return image.png().toBuffer();
    case 'jpeg':
      return image.jpeg({ quality: 90 }).toBuffer();
    case 'webp':
      return image.webp().toBuffer();
    case 'gif':
      return image.gif().toBuffer();
  }
}

/** An animated two-frame GIF. */
export async function animatedGif(width: number, height: number): Promise<Buffer> {
  const frame = (background: string) => sharp({ create: { width, height, channels: 3, background } }).png().toBuffer();
  return sharp([await frame('#22aa55'), await frame('#aa2255')], { join: { animated: true } })
    .gif({ delay: [100, 100] })
    .toBuffer();
}

/**
 * A syntactically valid PNG whose header claims huge dimensions but which has almost no pixel
 * data (a "decompression bomb" header). It must be refused from the header alone.
 */
export function hugePngHeader(width: number, height: number): Buffer {
  const crcTable = Array.from({ length: 256 }, (_, n) => {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
  });
  const crc = (data: Buffer) => {
    let c = 0xffffffff;
    for (const byte of data) c = crcTable[(c ^ byte) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunk = (type: string, data: Buffer) => {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'latin1'), data]);
    const sum = Buffer.alloc(4);
    sum.writeUInt32BE(crc(body));
    return Buffer.concat([length, body, sum]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 2; // RGB
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', Buffer.from([0x78, 0x9c, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01])),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}
