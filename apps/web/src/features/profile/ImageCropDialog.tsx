import { Minus, Move, Plus, RotateCcw, Upload } from 'lucide-react';
import { useCallback, useEffect, useId, useRef, useState, type KeyboardEvent, type PointerEvent } from 'react';
import { MEDIA_SPECS } from '@akeno/shared';
import { Button } from '../../components/Button';
import { cn } from '../../components/cn';
import { Modal } from '../../components/Modal';
import { InlineError } from '../../components/States';

export type CropKind = 'avatar' | 'banner';

/** Output size sent to the server (it re-encodes and makes the final variants). */
const OUTPUT: Record<CropKind, { width: number; height: number }> = {
  avatar: { width: 1024, height: 1024 },
  banner: { width: 2400, height: 800 },
};
/** Largest file the browser will even try to open (the upload itself is the much smaller crop). */
const MAX_SOURCE_BYTES = 40 * 1024 * 1024;
const MAX_ZOOM = 4;

interface View {
  zoom: number;
  /** Offset of the image centre from the frame centre, in frame pixels. */
  x: number;
  y: number;
}

/** Same signature allow-list as the server (JPEG, PNG, WebP, GIF); anything else is never decoded. */
export async function looksLikeSupportedImage(file: Blob): Promise<boolean> {
  const bytes = new Uint8Array(await file.slice(0, 12).arrayBuffer());
  if (bytes.length < 12) return false;
  const text = (start: number, end: number) => String.fromCharCode(...bytes.slice(start, end));
  if (bytes[0] === 0xff && bytes[1] === 0xd8 && bytes[2] === 0xff) return true;
  if (bytes[0] === 0x89 && text(1, 4) === 'PNG') return true;
  if (text(0, 4) === 'RIFF' && text(8, 12) === 'WEBP') return true;
  return text(0, 6) === 'GIF87a' || text(0, 6) === 'GIF89a';
}

function supportsWebpEncoding(): boolean {
  try {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 1;
    return canvas.toDataURL('image/webp').startsWith('data:image/webp');
  } catch {
    return false;
  }
}

/**
 * Mount it only while it is shown (state resets when it unmounts).
 *
 * Lets the user pick an image, then zoom and reposition it inside the final frame (circle for
 * profile pictures, 3:1 for banners) with mouse, touch or keyboard. Only the cropped result is
 * uploaded; the server validates, re-encodes and strips metadata again.
 */
export function ImageCropDialog({
  kind,
  open,
  onClose,
  onSave,
  saving,
  error,
}: {
  kind: CropKind;
  open: boolean;
  onClose: () => void;
  onSave: (file: Blob) => void;
  saving: boolean;
  error: string | null;
}) {
  const spec = MEDIA_SPECS[kind];
  const aspect = spec.aspect.width / spec.aspect.height;
  const inputId = useId();
  const frameRef = useRef<HTMLDivElement>(null);
  const [image, setImage] = useState<HTMLImageElement | null>(null);
  const [view, setView] = useState<View>({ zoom: 1, x: 0, y: 0 });
  const [frameWidth, setFrameWidth] = useState(0);
  const [localError, setLocalError] = useState<string | null>(null);
  const drag = useRef<{ id: number; startX: number; startY: number; view: View } | null>(null);
  const pinch = useRef<Map<number, { x: number; y: number }>>(new Map());

  useEffect(() => {
    const frame = frameRef.current;
    if (!frame) return;
    // ResizeObserver reports the initial size right after observe(), then every change.
    const observer = new ResizeObserver(() => setFrameWidth(frame.clientWidth));
    observer.observe(frame);
    return () => observer.disconnect();
  }, [image]);

  const frameHeight = frameWidth / aspect;

  /** Scale at zoom 1: the image covers the whole frame. */
  const baseScale = image && frameWidth > 0 ? Math.max(frameWidth / image.naturalWidth, frameHeight / image.naturalHeight) : 1;

  const clamp = useCallback(
    (next: View): View => {
      if (!image || frameWidth === 0) return next;
      const zoom = Math.min(MAX_ZOOM, Math.max(1, next.zoom));
      const width = image.naturalWidth * baseScale * zoom;
      const height = image.naturalHeight * baseScale * zoom;
      const maxX = Math.max(0, (width - frameWidth) / 2);
      const maxY = Math.max(0, (height - frameHeight) / 2);
      return { zoom, x: Math.min(maxX, Math.max(-maxX, next.x)), y: Math.min(maxY, Math.max(-maxY, next.y)) };
    },
    [image, frameWidth, frameHeight, baseScale],
  );

  const choose = async (file: File | undefined) => {
    setLocalError(null);
    if (!file) return;
    if (file.size > MAX_SOURCE_BYTES) {
      setLocalError('This file is too large. Choose an image under 40 MB.');
      return;
    }
    if (!(await looksLikeSupportedImage(file))) {
      setLocalError('This file could not be opened as an image. Use a JPEG, PNG, WebP or GIF image.');
      return;
    }
    const url = URL.createObjectURL(file);
    const next = new Image();
    next.onload = () => {
      const smallest = Math.min(next.naturalWidth / spec.aspect.width, next.naturalHeight / spec.aspect.height);
      if (smallest * spec.aspect.width < spec.minWidth || smallest * spec.aspect.height < spec.minHeight) {
        setLocalError(`This image is too small. Use at least ${spec.minWidth} × ${spec.minHeight} pixels.`);
        URL.revokeObjectURL(url);
        return;
      }
      setImage(next);
      setView({ zoom: 1, x: 0, y: 0 });
    };
    next.onerror = () => {
      URL.revokeObjectURL(url);
      setLocalError('This file could not be opened as an image. Use a JPEG, PNG, WebP or GIF image.');
    };
    next.src = url;
  };

  useEffect(() => {
    const current = image;
    return () => {
      if (current) URL.revokeObjectURL(current.src);
    };
  }, [image]);

  const onPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    if (!image) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    pinch.current.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (pinch.current.size === 1) drag.current = { id: event.pointerId, startX: event.clientX, startY: event.clientY, view };
  };
  const onPointerMove = (event: PointerEvent<HTMLDivElement>) => {
    if (!image) return;
    const points = pinch.current;
    if (points.has(event.pointerId) && points.size === 2) {
      const [a, b] = [...points.values()];
      const before = Math.hypot(a!.x - b!.x, a!.y - b!.y);
      points.set(event.pointerId, { x: event.clientX, y: event.clientY });
      const [c, d] = [...points.values()];
      const after = Math.hypot(c!.x - d!.x, c!.y - d!.y);
      if (before > 0) setView((current) => clamp({ ...current, zoom: current.zoom * (after / before) }));
      return;
    }
    const state = drag.current;
    if (!state || state.id !== event.pointerId) return;
    setView(clamp({ ...state.view, x: state.view.x + event.clientX - state.startX, y: state.view.y + event.clientY - state.startY }));
  };
  const onPointerUp = (event: PointerEvent<HTMLDivElement>) => {
    pinch.current.delete(event.pointerId);
    if (drag.current?.id === event.pointerId) drag.current = null;
  };
  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    const step = event.shiftKey ? 40 : 10;
    const moves: Record<string, Partial<View>> = {
      ArrowLeft: { x: view.x + step },
      ArrowRight: { x: view.x - step },
      ArrowUp: { y: view.y + step },
      ArrowDown: { y: view.y - step },
      '+': { zoom: view.zoom + 0.1 },
      '=': { zoom: view.zoom + 0.1 },
      '-': { zoom: view.zoom - 0.1 },
    };
    const move = moves[event.key];
    if (!move) return;
    event.preventDefault();
    setView(clamp({ ...view, ...move }));
  };

  const save = () => {
    if (!image || frameWidth === 0) return;
    const scale = baseScale * view.zoom;
    // The visible frame in source-image pixels.
    const sourceWidth = frameWidth / scale;
    const sourceHeight = frameHeight / scale;
    const sourceX = image.naturalWidth / 2 - view.x / scale - sourceWidth / 2;
    const sourceY = image.naturalHeight / 2 - view.y / scale - sourceHeight / 2;
    // Never enlarge: the output is at most the cropped source size.
    const ratio = Math.min(1, sourceWidth / OUTPUT[kind].width);
    const outWidth = Math.round(OUTPUT[kind].width * ratio);
    const outHeight = Math.round(OUTPUT[kind].height * ratio);
    const canvas = document.createElement('canvas');
    canvas.width = outWidth;
    canvas.height = outHeight;
    const context = canvas.getContext('2d');
    if (!context) {
      setLocalError('Your browser could not prepare the image.');
      return;
    }
    context.imageSmoothingQuality = 'high';
    context.drawImage(image, sourceX, sourceY, sourceWidth, sourceHeight, 0, 0, outWidth, outHeight);
    const type = supportsWebpEncoding() ? 'image/webp' : kind === 'avatar' ? 'image/png' : 'image/jpeg';
    canvas.toBlob(
      (blob) => {
        if (blob) onSave(blob);
        else setLocalError('Your browser could not prepare the image.');
      },
      type,
      0.92,
    );
  };

  const shownError = localError ?? error;
  const scale = baseScale * view.zoom;

  return (
    <Modal
      open={open}
      onClose={onClose}
      size="lg"
      title={kind === 'avatar' ? 'Profile picture' : 'Profile banner'}
      description={
        kind === 'avatar'
          ? 'Square images work best. You can zoom and move the picture inside the circle.'
          : 'A wide image (3:1) shown at the top of your profile. Zoom and move it to frame it.'
      }
      footer={
        <>
          <Button variant="ghost" onClick={onClose} disabled={saving}>
            Cancel
          </Button>
          <Button variant="primary" onClick={save} disabled={!image} loading={saving}>
            Save {kind === 'avatar' ? 'picture' : 'banner'}
          </Button>
        </>
      }
    >
      <div className="flex flex-col gap-4">
        {image ? (
          <>
            <div
              ref={frameRef}
              role="application"
              aria-label="Image position. Drag, pinch or use the arrow keys to move; plus and minus to zoom."
              tabIndex={0}
              onPointerDown={onPointerDown}
              onPointerMove={onPointerMove}
              onPointerUp={onPointerUp}
              onPointerCancel={onPointerUp}
              onKeyDown={onKeyDown}
              onWheel={(event) => setView(clamp({ ...view, zoom: view.zoom * (event.deltaY < 0 ? 1.06 : 0.94) }))}
              className={cn(
                'relative mx-auto w-full cursor-grab touch-none select-none overflow-hidden rounded-xl bg-canvas active:cursor-grabbing',
                kind === 'avatar' && 'max-w-[420px]',
              )}
              style={{ aspectRatio: `${spec.aspect.width} / ${spec.aspect.height}` }}
              data-testid="crop-frame"
            >
              <img
                src={image.src}
                alt=""
                draggable={false}
                className="pointer-events-none absolute left-1/2 top-1/2 max-w-none"
                style={{
                  width: image.naturalWidth * scale,
                  height: image.naturalHeight * scale,
                  transform: `translate(calc(-50% + ${view.x}px), calc(-50% + ${view.y}px))`,
                }}
              />
              {kind === 'avatar' ? (
                <div className="pointer-events-none absolute inset-0 rounded-full shadow-[0_0_0_9999px_rgb(3_6_14/0.6)] ring-2 ring-white/70" aria-hidden />
              ) : (
                <div className="pointer-events-none absolute inset-0 ring-2 ring-inset ring-white/60" aria-hidden />
              )}
              <span className="pointer-events-none absolute bottom-2 left-2 inline-flex items-center gap-1 rounded-md bg-black/55 px-2 py-1 text-xs text-white">
                <Move aria-hidden className="size-3.5" /> Drag to move
              </span>
            </div>
            <div className="flex items-center gap-3">
              <Button size="icon" variant="ghost" aria-label="Zoom out" onClick={() => setView(clamp({ ...view, zoom: view.zoom - 0.2 }))}>
                <Minus aria-hidden className="size-4" />
              </Button>
              <label className="sr-only" htmlFor={`${inputId}-zoom`}>
                Zoom
              </label>
              <input
                id={`${inputId}-zoom`}
                type="range"
                min={1}
                max={MAX_ZOOM}
                step={0.01}
                value={view.zoom}
                onChange={(event) => setView(clamp({ ...view, zoom: Number(event.target.value) }))}
                className="h-2 flex-1 cursor-pointer accent-[var(--akeno-accent)]"
              />
              <Button size="icon" variant="ghost" aria-label="Zoom in" onClick={() => setView(clamp({ ...view, zoom: view.zoom + 0.2 }))}>
                <Plus aria-hidden className="size-4" />
              </Button>
              <Button size="icon" variant="ghost" aria-label="Reset position" onClick={() => setView({ zoom: 1, x: 0, y: 0 })}>
                <RotateCcw aria-hidden className="size-4" />
              </Button>
            </div>
            <div className="flex items-center gap-4">
              {kind === 'avatar' ? <p className="text-sm text-muted">Preview</p> : null}
              {kind === 'avatar' && frameWidth > 0 ? (
                <div className="flex items-end gap-3" aria-hidden>
                  {[96, 48, 32].map((size) => (
                    <div key={size} className="relative overflow-hidden rounded-full bg-canvas" style={{ width: size, height: size }}>
                      <img
                        src={image.src}
                        alt=""
                        className="absolute left-1/2 top-1/2 max-w-none"
                        style={{
                          width: (image.naturalWidth * scale * size) / frameWidth,
                          height: (image.naturalHeight * scale * size) / frameWidth,
                          transform: `translate(calc(-50% + ${(view.x * size) / frameWidth}px), calc(-50% + ${(view.y * size) / frameWidth}px))`,
                        }}
                      />
                    </div>
                  ))}
                </div>
              ) : null}
              <label htmlFor={inputId} className="ml-auto cursor-pointer text-sm font-medium text-accent hover:underline">
                Choose another image
              </label>
            </div>
          </>
        ) : (
          <label
            htmlFor={inputId}
            className="flex cursor-pointer flex-col items-center justify-center gap-2 rounded-xl border-2 border-dashed border-line-strong bg-surface-2 px-6 py-12 text-center hover:border-accent"
            onDragOver={(event) => event.preventDefault()}
            onDrop={(event) => {
              event.preventDefault();
              void choose(event.dataTransfer.files[0]);
            }}
          >
            <Upload aria-hidden className="size-8 text-accent" />
            <span className="font-medium">Choose an image</span>
            <span className="text-sm text-muted">
              JPEG, PNG, WebP or GIF · at least {spec.minWidth} × {spec.minHeight} pixels. On a phone you can take a new photo or pick one from your library.
            </span>
          </label>
        )}
        <input
          id={inputId}
          type="file"
          accept="image/jpeg,image/png,image/webp,image/gif"
          className="sr-only"
          data-testid={`${kind}-file-input`}
          onChange={(event) => {
            void choose(event.target.files?.[0]);
            event.target.value = '';
          }}
        />
        <InlineError message={shownError} />
        <p className="text-xs text-subtle">
          The server re-encodes your image and removes camera data such as location (EXIF). Animated images are saved as a still picture.
        </p>
      </div>
    </Modal>
  );
}
