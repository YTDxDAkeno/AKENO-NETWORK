import type { ButtonHTMLAttributes, ReactNode, Ref } from 'react';
import { Link, type LinkProps } from 'react-router';
import { cn } from './cn';
import { Spinner } from './Spinner';

type Variant = 'primary' | 'secondary' | 'ghost' | 'danger' | 'subtle';
type Size = 'sm' | 'md' | 'lg' | 'icon';

const base =
  'inline-flex items-center justify-center gap-2 rounded-xl font-medium transition-[background-color,box-shadow,color,transform] duration-150 select-none disabled:cursor-not-allowed disabled:opacity-55 active:translate-y-px';

const variants: Record<Variant, string> = {
  primary: 'bg-accent text-on-accent shadow-sm hover:bg-accent-strong',
  secondary: 'border border-line-strong bg-surface-2 text-ink hover:bg-surface-3',
  ghost: 'text-muted hover:bg-surface-2 hover:text-ink',
  danger: 'bg-danger text-white hover:brightness-110',
  subtle: 'bg-accent-soft text-accent hover:bg-accent/25',
};

const sizes: Record<Size, string> = {
  sm: 'h-8 px-3 text-sm',
  md: 'h-10 px-4 text-sm',
  lg: 'h-12 px-6 text-base',
  icon: 'size-9',
};

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: Variant;
  size?: Size;
  loading?: boolean;
  icon?: ReactNode;
  ref?: Ref<HTMLButtonElement>;
}

export function Button({
  variant = 'secondary',
  size = 'md',
  loading = false,
  icon,
  className,
  children,
  disabled,
  type = 'button',
  ...props
}: ButtonProps) {
  return (
    <button
      type={type}
      className={cn(base, variants[variant], sizes[size], className)}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      {...props}
    >
      {loading ? <Spinner label="Working" /> : icon}
      {children}
    </button>
  );
}

export function ButtonLink({
  variant = 'secondary',
  size = 'md',
  icon,
  className,
  children,
  ...props
}: LinkProps & { variant?: Variant; size?: Size; icon?: ReactNode }) {
  return (
    <Link className={cn(base, variants[variant], sizes[size], className)} {...props}>
      {icon}
      {children}
    </Link>
  );
}
