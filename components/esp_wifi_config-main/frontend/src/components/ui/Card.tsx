import { ComponentChildren } from 'preact';
import './Card.css';

interface CardProps {
  title?: string;
  action?: ComponentChildren;
  children: ComponentChildren;
}

export function Card({ title, action, children }: CardProps) {
  return (
    <div class="card">
      {title && (
        <div class="card-header">
          <h2 class="card-title">{title}</h2>
          {action}
        </div>
      )}
      <div class="card-body">{children}</div>
    </div>
  );
}
