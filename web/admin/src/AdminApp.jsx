import React, { useState } from 'react';
import RegCodePage from './pages/RegCodePage';
import UserManagerPage from './pages/UserManagerPage';

const PAGES = {
  regcode: { label: '注册码生成', icon: '⌁' },
  users: { label: '用户管理', icon: '♙' },
};

function initialPage() {
  const path = window.location.pathname;
  if (path.includes('user_manager')) return 'users';
  return 'regcode';
}

export default function AdminApp() {
  const [page, setPage] = useState(initialPage);

  return (
    <div className="admin-shell">
      <aside className="sidebar">
        <div className="admin-brand">
          <span className="admin-mark">✦</span>
          <span>jettyCat 管理</span>
        </div>
        <div className="sidebar-label">Workspace</div>
        <nav className="menu" aria-label="管理菜单">
          {Object.entries(PAGES).map(([key, meta]) => (
            <button
              key={key}
              className={`menu-item${page === key ? ' active' : ''}`}
              type="button"
              onClick={() => setPage(key)}
            >
              <span className="menu-icon">{meta.icon}</span>
              <span>{meta.label}</span>
            </button>
          ))}
        </nav>
      </aside>
      <main className="admin-content">
        <header className="topline">
          <div>
            <p className="eyebrow">Administration</p>
            <h1>管理工作台</h1>
            <p>管理注册入口和平台用户。</p>
          </div>
        </header>
        {page === 'regcode' ? <RegCodePage /> : <UserManagerPage />}
      </main>
    </div>
  );
}
