import React from 'react';

const SYMBOLS = {
  logo: '<rect x="3" y="3" width="26" height="26" rx="8" fill="url(#logoGrad)"/><path d="M11 19a4 4 0 0 1 4-4h2a4 4 0 0 1 4 4v1a3 3 0 0 1-3 3h-4a3 3 0 0 1-3-3v-1Z" fill="#fff"/><path d="M12.5 12.5h.01M19.5 12.5h.01" stroke="#fff" stroke-width="2.4" stroke-linecap="round"/><defs><linearGradient id="logoGrad" x1="3" y1="3" x2="29" y2="29" gradientUnits="userSpaceOnUse"><stop stop-color="#7e74eb"/><stop offset="1" stop-color="#a275d8"/></linearGradient></defs>',
  chat: '<path d="M21 11.5a8.5 8.5 0 0 1-12.5 7.4L3 21l2.1-5.5A8.5 8.5 0 1 1 21 11.5Z"/><path d="M8.5 11.5h.01M12 11.5h.01M15.5 11.5h.01"/>',
  contacts: '<circle cx="9" cy="8" r="3.4"/><path d="M2.5 20a6.5 6.5 0 0 1 13 0"/><path d="M16 5.2a3.4 3.4 0 0 1 0 6.1M17.5 14.3a6.5 6.5 0 0 1 4 5.7"/>',
  live: '<rect x="3" y="5" width="12" height="14" rx="3"/><path d="m15 10 6-3.5v11L15 14"/>',
  analytics: '<path d="M4 20h16"/><path d="M6 20v-6M10 20V9M14 20v-9M18 20V5"/>',
  settings: '<circle cx="12" cy="12" r="3"/><path d="M12 2.8v2.4M12 18.8v2.4M4.5 4.5l1.7 1.7M17.8 17.8l1.7 1.7M2.8 12h2.4M18.8 12h2.4M4.5 19.5l1.7-1.7M17.8 6.2l1.7-1.7"/>',
  logout: '<path d="M9 21H6a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3"/><path d="M16 17l5-5-5-5M21 12H9"/>',
  search: '<circle cx="11" cy="11" r="6.5"/><path d="m20 20-3.5-3.5"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  send: '<path d="m22 2-7 20-4-9-9-4 20-7Z"/><path d="M22 2 11 13"/>',
  close: '<path d="M6 6l12 12M18 6 6 18"/>',
  chevron: '<path d="m6 9 6 6 6-6"/>',
  connection: '<path d="M7 12a5 5 0 0 1 10 0"/><path d="M4.5 9.5a8.5 8.5 0 0 1 15 0"/><circle cx="12" cy="15.5" r="1.6"/>',
  user: '<circle cx="12" cy="8" r="3.6"/><path d="M4.5 20a7.5 7.5 0 0 1 15 0"/>',
  spark: '<path d="M12 3v5M12 16v5M3 12h5M16 12h5M6.2 6.2 9.6 9.6M14.4 14.4l3.4 3.4M6.2 17.8l3.4-3.4M14.4 9.6l3.4-3.4"/>',
  more: '<circle cx="5" cy="12" r="1.8"/><circle cx="12" cy="12" r="1.8"/><circle cx="19" cy="12" r="1.8"/>',
  image: '<rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="m21 15-5-5L5 21"/>',
  file: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8Z"/><path d="M14 2v6h6"/><path d="M16 13H8"/><path d="M16 17H8"/><path d="M10 9H8"/>',
};

export function IconSprite() {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" style={{ display: 'none' }} aria-hidden="true">
      {Object.entries(SYMBOLS).map(([name, body]) => (
        <symbol
          key={name}
          id={`icon-${name}`}
          viewBox={name === 'logo' ? '0 0 32 32' : '0 0 24 24'}
          fill="none"
          stroke="currentColor"
          strokeWidth="1.8"
          strokeLinecap="round"
          strokeLinejoin="round"
          dangerouslySetInnerHTML={{ __html: body }}
        />
      ))}
    </svg>
  );
}
