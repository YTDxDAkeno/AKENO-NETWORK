import { useId, type InputHTMLAttributes, type ReactNode, type Ref, type SelectHTMLAttributes, type TextareaHTMLAttributes } from 'react';
import { cn } from './cn';

const control =
  'w-full rounded-xl border border-line-strong bg-surface-2 px-3.5 text-sm text-ink placeholder:text-subtle transition-colors focus:border-accent focus:outline-none focus-visible:outline-none focus:ring-2 focus:ring-accent/30 disabled:opacity-60 aria-[invalid=true]:border-danger';

interface FieldShellProps {
  id: string;
  label: string;
  hint?: ReactNode;
  error?: string;
  required?: boolean;
  children: ReactNode;
  className?: string;
  labelHidden?: boolean;
}

function FieldShell({ id, label, hint, error, required, children, className, labelHidden }: FieldShellProps) {
  return (
    <div className={cn('flex flex-col gap-1.5', className)}>
      <div className={cn('flex items-baseline gap-1', labelHidden && 'sr-only')}>
        <label htmlFor={id} className="text-sm font-medium text-ink">
          {label}
        </label>
        {required ? (
          <span aria-hidden className="text-sm text-danger">
            *
          </span>
        ) : null}
      </div>
      {children}
      {error ? (
        <p id={`${id}-error`} className="text-xs text-danger" role="alert">
          {error}
        </p>
      ) : hint ? (
        <p id={`${id}-hint`} className="text-xs text-muted">
          {hint}
        </p>
      ) : null}
    </div>
  );
}

type Common = { label: string; hint?: ReactNode; error?: string; labelHidden?: boolean; containerClassName?: string };

export function TextField({
  label,
  hint,
  error,
  labelHidden,
  containerClassName,
  className,
  id: providedId,
  ref,
  ...props
}: Common & InputHTMLAttributes<HTMLInputElement> & { ref?: Ref<HTMLInputElement> }) {
  const generated = useId();
  const id = providedId ?? generated;
  return (
    <FieldShell id={id} label={label} hint={hint} error={error} required={props.required} labelHidden={labelHidden} className={containerClassName}>
      <input
        ref={ref}
        id={id}
        className={cn(control, 'h-11', className)}
        aria-invalid={error ? true : undefined}
        aria-describedby={error ? `${id}-error` : hint ? `${id}-hint` : undefined}
        {...props}
      />
    </FieldShell>
  );
}

export function TextArea({
  label,
  hint,
  error,
  labelHidden,
  containerClassName,
  className,
  id: providedId,
  ...props
}: Common & TextareaHTMLAttributes<HTMLTextAreaElement>) {
  const generated = useId();
  const id = providedId ?? generated;
  return (
    <FieldShell id={id} label={label} hint={hint} error={error} required={props.required} labelHidden={labelHidden} className={containerClassName}>
      <textarea
        id={id}
        className={cn(control, 'min-h-24 py-2.5 leading-relaxed', className)}
        aria-invalid={error ? true : undefined}
        aria-describedby={error ? `${id}-error` : hint ? `${id}-hint` : undefined}
        {...props}
      />
    </FieldShell>
  );
}

export function SelectField({
  label,
  hint,
  error,
  labelHidden,
  containerClassName,
  className,
  id: providedId,
  options,
  ...props
}: Common & SelectHTMLAttributes<HTMLSelectElement> & { options: Array<{ value: string; label: string }> }) {
  const generated = useId();
  const id = providedId ?? generated;
  return (
    <FieldShell id={id} label={label} hint={hint} error={error} required={props.required} labelHidden={labelHidden} className={containerClassName}>
      <select
        id={id}
        className={cn(control, 'h-11 appearance-none bg-[length:16px] bg-[right_0.75rem_center] bg-no-repeat pr-9', className)}
        style={{
          backgroundImage:
            "url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%239aa5c4' stroke-width='2'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E\")",
        }}
        aria-invalid={error ? true : undefined}
        {...props}
      >
        {options.map((option) => (
          <option key={option.value} value={option.value}>
            {option.label}
          </option>
        ))}
      </select>
    </FieldShell>
  );
}

export function Switch({
  checked,
  onChange,
  label,
  description,
  disabled,
}: {
  checked: boolean;
  onChange: (value: boolean) => void;
  label: string;
  description?: string;
  disabled?: boolean;
}) {
  const id = useId();
  return (
    <div className="flex items-start justify-between gap-4">
      <div>
        <p id={`${id}-label`} className="text-sm font-medium text-ink">
          {label}
        </p>
        {description ? <p className="mt-0.5 text-xs text-muted">{description}</p> : null}
      </div>
      <button
        type="button"
        role="switch"
        aria-checked={checked}
        aria-labelledby={`${id}-label`}
        disabled={disabled}
        onClick={() => onChange(!checked)}
        className={cn(
          'relative inline-flex h-6 w-11 shrink-0 items-center rounded-full border border-line-strong transition-colors',
          checked ? 'bg-accent' : 'bg-surface-3',
        )}
      >
        <span
          className={cn(
            'inline-block size-4.5 rounded-full bg-white shadow transition-transform',
            checked ? 'translate-x-5.5' : 'translate-x-0.5',
          )}
        />
      </button>
    </div>
  );
}
