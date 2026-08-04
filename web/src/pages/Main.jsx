import React, { useEffect, useState } from 'react';
import { useChat } from '../ws';
import ChatSection from '../sections/ChatSection';
import ContactsSection from '../sections/ContactsSection';
import LiveSection from '../sections/LiveSection';
import AnalyticsSection from '../sections/AnalyticsSection';
import SettingsSection from '../sections/SettingsSection';

const SECTION_META = {
  chat: { label: '聊天' },
  contacts: { label: '联系人' },
  live: { label: '直播' },
  analytics: { label: '互动看板' },
  settings: { label: '设置' },
};

const SECTION_ORDER = ['chat', 'contacts', 'live', 'analytics'];

export default function MainPage() {
  const { connected, myId, myName, conversations, notices, dismissNotice, addConversation, connect, disconnect, unread, lastMessages, setActiveKey, clearUnread } = useChat();
  const [section, setSection] = useState('chat');
  const [activeConversation, setActiveConversation] = useState(null);
  const [menuOpen, setMenuOpen] = useState(false);
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [addOpen, setAddOpen] = useState(false);
  const [newUserId, setNewUserId] = useState('');

  useEffect(() => {
    connect();
    return () => disconnect();
  }, [connect, disconnect]);

  // When leaving the chat section (or opening a conversation), sync the
  // "active conversation" key used by the provider to decide whether an
  // incoming message should raise a notification.
  useEffect(() => {
    if (section !== 'chat') {
      setActiveKey(null);
    }
  }, [section, setActiveKey]);

  useEffect(() => {
    let saved = 'chat';
    try {
      saved = localStorage.getItem('jettycat.workspace.section') || 'chat';
    } catch (error) {
      /* ignore */
    }
    if (SECTION_META[saved]) setSection(saved);
  }, []);

  const selectSection = (next) => {
    if (!SECTION_META[next]) return;
    setSection(next);
    setActiveConversation(null);
    try {
      localStorage.setItem('jettycat.workspace.section', next);
    } catch (error) {
      /* ignore */
    }
    if (window.innerWidth <= 820) setSidebarOpen(true);
  };

  const selectConversation = (item) => {
    if (item.add) {
      setAddOpen(true);
      return;
    }
    setActiveConversation(item);
    clearUnread(`${item.kind}:${item.id}`);
    if (window.innerWidth <= 820) setSidebarOpen(false);
  };

  const openAdd = () => {
    if (section === 'chat' || section === 'contacts') setAddOpen(true);
  };

  const startConversation = () => {
    const value = Number(newUserId);
    if (!Number.isInteger(value) || value < 1) return;
    addConversation('user', value);
    setAddOpen(false);
    setNewUserId('');
    setActiveConversation({ kind: 'user', id: value });
    if (window.innerWidth <= 820) setSidebarOpen(false);
  };

  const logout = () => {
    disconnect();
    window.location.href = '/login';
  };

  const renderSection = () => {
    switch (section) {
      case 'chat':
        return <ChatSection active={activeConversation} onSelect={setActiveConversation} />;
      case 'contacts':
        return <ContactsSection onOpenChat={(item) => { setActiveConversation(item); setSection('chat'); }} />;
      case 'live':
        return <LiveSection />;
      case 'analytics':
        return <AnalyticsSection />;
      case 'settings':
        return <SettingsSection />;
      default:
        return null;
    }
  };

  const sidebarItems = buildSidebarItems(section, conversations, activeConversation, unread, lastMessages);
  const isActive = (key) => key === section;

  return (
    <div className="workspace-page">
      <header className="workspace-topbar">
        <a className="workspace-brand" href="/main" aria-label="jettyCat 工作台">
          <span className="workspace-logo">
            <svg className="icon" focusable="false" aria-hidden="true"><use href="#icon-logo" /></svg>
          </span>
          <span className="workspace-brand-name">jettyCat</span>
        </a>
        <div className="workspace-profile">
          <span className={`top-connection ${connected ? 'online' : 'offline'}`}>
            <i></i>
            {connected ? '已连接' : '连接断开'}
          </span>
          <button
            className="profile-button"
            type="button"
            aria-expanded={menuOpen}
            onClick={() => setMenuOpen((v) => !v)}
          >
            <span className="profile-avatar">{initials(myName)}</span>
            <span className="profile-copy">
              <strong>{myId ? `ID ${myId}` : 'ID --'}</strong>
            </span>
            <svg className="profile-chevron" focusable="false" aria-hidden="true"><use href="#icon-chevron" /></svg>
          </button>
          {menuOpen && (
            <div className="profile-menu">
              <button type="button" onClick={() => { setMenuOpen(false); selectSection('settings'); }}>账户设置</button>
              <button type="button" onClick={() => { setMenuOpen(false); logout(); }}>退出登录</button>
            </div>
          )}
        </div>
      </header>

      <main className="workspace-shell">
        <nav className="rail" aria-label="主导航">
          <div className="rail-primary">
            {SECTION_ORDER.map((key) => (
              <button
                key={key}
                className={`rail-item${isActive(key) ? ' active' : ''}`}
                type="button"
                aria-label={SECTION_META[key].label}
                title={SECTION_META[key].label}
                onClick={() => selectSection(key)}
              >
                <svg className="rail-icon" focusable="false" aria-hidden="true"><use href={`#icon-${key}`} /></svg>
                <span className="rail-label">{SECTION_META[key].label}</span>
              </button>
            ))}
          </div>
          <div className="rail-secondary">
            <button className={`rail-item${isActive('settings') ? ' active' : ''}`} type="button" aria-label="设置" title="设置" onClick={() => selectSection('settings')}>
              <svg className="rail-icon" focusable="false" aria-hidden="true"><use href="#icon-settings" /></svg>
              <span className="rail-label">设置</span>
            </button>
            <button className="rail-item" type="button" aria-label="退出登录" title="退出登录" onClick={logout}>
              <svg className="rail-icon" focusable="false" aria-hidden="true"><use href="#icon-logout" /></svg>
              <span className="rail-label">退出</span>
            </button>
          </div>
        </nav>

        <Sidebar
          section={section}
          items={sidebarItems}
          activeKey={activeConversation ? `${activeConversation.kind}:${activeConversation.id}` : null}
          open={sidebarOpen}
          onSelect={section === 'chat' ? selectConversation : (item) => selectSection(section)}
          onAdd={openAdd}
        />

        <section className="workspace-content">{renderSection()}</section>
      </main>

      {addOpen && (
        <div className="module-dialog">
          <div className="module-dialog-card">
            <button className="dialog-close" type="button" aria-label="关闭" onClick={() => setAddOpen(false)}>
              <svg className="icon" focusable="false" aria-hidden="true"><use href="#icon-close" /></svg>
            </button>
            <p className="eyebrow">New conversation</p>
            <h2>开始一段新对话</h2>
            <label>
              对方用户 ID
              <input type="number" min="1" placeholder="例如 20" value={newUserId} onChange={(e) => setNewUserId(e.target.value)} autoFocus />
            </label>
            <div className="dialog-actions">
              <button className="button secondary" type="button" onClick={() => setAddOpen(false)}>取消</button>
              <button className="button" type="button" onClick={startConversation}>开始聊天</button>
            </div>
          </div>
        </div>
      )}

      <div className="notice-stack" aria-live="polite">
        {notices.map((notice) => (
          <div key={notice.id} className="notice">
            <span className="notice-avatar">{initials(notice.sender || notice.title)}</span>
            <span className="notice-copy">
              <strong>{notice.title}</strong>
              <p>{notice.content}</p>
            </span>
            <button className="notice-close" type="button" onClick={() => dismissNotice(notice.id)}>×</button>
          </div>
        ))}
      </div>
    </div>
  );
}

function initials(name) {
  return String(name || '?').trim().slice(0, 2).toUpperCase();
}

function truncate(text) {
  const s = String(text == null ? '' : text).replace(/\s+/g, ' ').trim();
  return s.length > 18 ? s.slice(0, 18) + '…' : s;
}

function buildSidebarItems(section, conversations, activeConversation, unread, lastMessages) {
  if (section === 'chat') {
    return conversations
      .filter((item) => item.kind === 'user')
      .map((item) => {
        const key = `${item.kind}:${item.id}`;
        const last = lastMessages[key];
        return {
          key,
          kind: item.kind,
          id: item.id,
          title: `用户 ${item.id}`,
          sub: last ? truncate(last) : '点击进入私聊',
          unread: unread[key] || 0,
          active: activeConversation && activeConversation.kind === item.kind && Number(activeConversation.id) === Number(item.id),
        };
      });
  }
  if (section === 'contacts') {
    return [
      { key: 'mine', title: '我的联系人', sub: '通过用户 ID 开始聊天' },
      { key: 'recent', title: '最近联系人', sub: '实时互动联系人' },
    ];
  }
  if (section === 'live') {
    return [
      { key: 'mine', title: '我的直播间', sub: '直播控制与房间状态', kind: 'live', time: '待机' },
      { key: 'discover', title: '发现直播', sub: '浏览实时房间', kind: 'live', time: 'Live' },
    ];
  }
  if (section === 'analytics') {
    return [{ key: 'overview', title: '互动总览', sub: '消息、在线与房间状态' }];
  }
  return [{ key: 'prefs', title: '账户偏好', sub: '主题、通知与连接设置' }];
}

function Sidebar({ section, items, activeKey, open, onSelect, onAdd }) {
  const [query, setQuery] = useState('');
  const list = section === 'chat'
    ? items.filter((item) => String(item.id).includes(query))
    : items.filter((item) => !query || item.title.toLowerCase().includes(query));

  return (
    <aside className={`workspace-sidebar${open ? ' open' : ''}`}>
      <div className="sidebar-toolbar">
        <label className="search-box">
          <svg className="icon" focusable="false" aria-hidden="true"><use href="#icon-search" /></svg>
          <input type="search" placeholder="搜索" value={query} onChange={(e) => setQuery(e.target.value)} />
        </label>
        <button className="icon-button" type="button" aria-label="新建" onClick={onAdd}>
          <svg focusable="false" aria-hidden="true"><use href="#icon-plus" /></svg>
        </button>
      </div>
      <div className="sidebar-list">
        {list.length === 0 && (
          <div className="section-list-empty">
            这里暂时没有内容
            <br />
            点击右上角开始添加
          </div>
        )}
        {list.map((item) => (
          <button
            key={item.key}
            type="button"
            className={`section-list-item${item.kind === 'live' ? ' live' : ''}${item.active || activeKey === item.key ? ' active' : ''}`}
            onClick={() => onSelect(item)}
          >
            <span className="list-avatar">{initials(item.title)}</span>
            <span className="section-list-copy">
              <strong>{item.title}</strong>
              <small>{item.sub}</small>
            </span>
            {item.unread > 0 ? (
              <span className="list-badge">{item.unread > 99 ? '99+' : item.unread}</span>
            ) : (
              <span className="list-time">{item.time || ''}</span>
            )}
          </button>
        ))}
      </div>
    </aside>
  );
}
