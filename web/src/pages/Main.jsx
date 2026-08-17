import React, { useEffect, useState } from 'react';
import { useChat, lastActiveOf } from '../ws';
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
  const { connected, myId, myName, myAvatarUrl, conversations, activity, sortMode, changeSortMode, notices, dismissNotice, addFriend, removeFriend, addGroup, removeGroup, connect, disconnect, unread, lastMessages, setActiveKey, clearUnread, showNotice, fetchUserProfile } = useChat();
  const [section, setSection] = useState('chat');
  const [activeConversation, setActiveConversation] = useState(null);
  const [menuOpen, setMenuOpen] = useState(false);
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [addOpen, setAddOpen] = useState(false);
  const [addMode, setAddMode] = useState('friend');
  const [newTargetId, setNewTargetId] = useState('');
  const [addBusy, setAddBusy] = useState(false);

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
    if (section === 'chat' || section === 'contacts') {
      setAddOpen(true);
      setAddMode(section === 'contacts' ? 'friend' : addMode);
    }
  };

  const submitAdd = async () => {
    const value = Number(newTargetId);
    if (!Number.isInteger(value) || value < 1) return;
    setAddBusy(true);
    try {
      const result = addMode === 'group' ? await addGroup(value) : await addFriend(value);
      if (!result || result.code !== 200) {
        showNotice(addMode === 'group' ? '加入群聊' : '添加好友', (result && result.msg) || '操作失败, 请稍后重试');
        return;
      }
      setAddOpen(false);
      setNewTargetId('');
      setActiveConversation({ kind: addMode === 'group' ? 'group' : 'user', id: value });
      if (window.innerWidth <= 820) setSidebarOpen(false);
    } finally {
      setAddBusy(false);
    }
  };

  const handleRemove = async (item) => {
    const label = item.kind === 'group' ? `退出群聊「${item.name}」?` : `删除好友「${item.name}」?`;
    if (!window.confirm(label)) return;
    const result = item.kind === 'group' ? await removeGroup(item.id) : await removeFriend(item.id);
    if (!result || result.code !== 200) {
      showNotice(item.kind === 'group' ? '退出群聊' : '删除好友', (result && result.msg) || '操作失败, 请稍后重试');
      return;
    }
    if (activeConversation && activeConversation.kind === item.kind && Number(activeConversation.id) === Number(item.id)) {
      setActiveConversation(null);
    }
  };

  const logout = () => {
    disconnect();
    window.location.href = '/login';
  };

  const renderSection = () => {
    switch (section) {
      case 'chat': {
        const resolved = resolveConversation(activeConversation, conversations);
        return <ChatSection active={resolved} onSelect={setActiveConversation} />;
      }
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

  const sidebarItems = buildSidebarItems(section, conversations, activeConversation, unread, lastMessages, activity, sortMode);
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
            <span className="profile-avatar">
              {myAvatarUrl ? (
                <img className="profile-avatar-img" src={myAvatarUrl} alt={myName} />
              ) : (
                initials(myName)
              )}
            </span>
            <span className="profile-copy">
              <strong>{myName || 'jettyCat 用户'}</strong>
            </span>
            <svg className="profile-chevron" focusable="false" aria-hidden="true"><use href="#icon-chevron" /></svg>
          </button>
          {menuOpen && (
            <div className="profile-menu">
              {myId && <div className="profile-info-line">ID {myId}</div>}
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
          onRemove={section === 'chat' ? handleRemove : null}
          sortMode={section === 'chat' ? sortMode : null}
          onSortModeChange={changeSortMode}
          fetchUserProfile={fetchUserProfile}
          myId={myId}
        />

        <section className="workspace-content">{renderSection()}</section>
      </main>

      {addOpen && (
        <>
          <div className="module-dialog-backdrop" onClick={() => setAddOpen(false)} />
          <div className="module-dialog">
            <div className="module-dialog-card">
            <p className="eyebrow">New relationship</p>
            <h2>{addMode === 'group' ? '加入群聊' : '添加好友'}</h2>
            <div className="mode-switch" role="group" aria-label="添加类型">
              <button className={`mode-btn${addMode === 'friend' ? ' active' : ''}`} type="button" onClick={() => setAddMode('friend')}>
                添加好友
              </button>
              <button className={`mode-btn${addMode === 'group' ? ' active' : ''}`} type="button" onClick={() => setAddMode('group')}>
                加入群聊
              </button>
            </div>
            <label>
              {addMode === 'group' ? '群聊 ID' : '用户 ID'}
              <input
                type="number"
                min="1"
                placeholder={addMode === 'group' ? '例如 1' : '例如 20'}
                value={newTargetId}
                onChange={(e) => setNewTargetId(e.target.value)}
                autoFocus
              />
            </label>
            <div className="dialog-actions">
              <button className="button secondary" type="button" onClick={() => setAddOpen(false)}>取消</button>
              <button className="button" type="button" onClick={submitAdd} disabled={addBusy}>
                {addBusy ? '处理中...' : addMode === 'group' ? '加入' : '添加'}
              </button>
            </div>
            </div>
          </div>
        </>
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

// 用会话列表中的最新元数据(名称/身份等)补全当前激活的会话对象。
// 新建会话时只有 kind/id, 列表刷新后这里的 name 会自动带上真实名称。
function resolveConversation(active, conversations) {
  if (!active) return null;
  const match = conversations.find(
    (item) => item.kind === active.kind && Number(item.id) === Number(active.id)
  );
  return match ? { ...active, ...match } : active;
}

function truncate(text) {
  const s = String(text == null ? '' : text).replace(/\s+/g, ' ').trim();
  return s.length > 18 ? s.slice(0, 18) + '…' : s;
}

// 聊天列表排序: 默认按名称(拼音/字母), 切换"按活跃"后按会话最近活跃时间降序。
// 活跃时间来自前端维护的 activity 记录, 详见 ws.jsx 中 ACTIVITY_SOURCES。
function sortChatItems(items, sortMode) {
  const sorted = [...items];
  if (sortMode === 'active') {
    sorted.sort((a, b) => (b.lastActive || 0) - (a.lastActive || 0));
  } else {
    sorted.sort((a, b) => a.name.localeCompare(b.name, 'zh-Hans-CN'));
  }
  return sorted;
}

function buildSidebarItems(section, conversations, activeConversation, unread, lastMessages, activity, sortMode) {
  if (section === 'chat') {
    const items = conversations.map((item) => {
      const key = `${item.kind}:${item.id}`;
      const last = lastMessages[key];
      const lastActive = lastActiveOf(activity[key]);
      return {
        key,
        kind: item.kind,
        id: item.id,
        name: item.name || (item.kind === 'group' ? `群组 ${item.id}` : `用户 ${item.id}`),
        sub: last ? truncate(last) : (item.kind === 'group' ? '点击进入群聊' : '点击进入私聊'),
        unread: unread[key] || 0,
        lastActive,
        active: activeConversation && activeConversation.kind === item.kind && Number(activeConversation.id) === Number(item.id),
        avatarUrl: item.avatarUrl || '',
      };
    });
    return sortChatItems(items, sortMode);
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

function formatActivityTime(ts) {
  if (!ts) return '';
  const diff = Date.now() - Number(ts);
  const minute = 60 * 1000;
  const hour = 60 * minute;
  const day = 24 * hour;
  if (diff < minute) return '刚刚';
  if (diff < hour) return `${Math.floor(diff / minute)} 分钟前`;
  if (diff < day) return `${Math.floor(diff / hour)} 小时前`;
  if (diff < 7 * day) return `${Math.floor(diff / day)} 天前`;
  const date = new Date(Number(ts));
  return `${date.getMonth() + 1}月${date.getDate()}日`;
}

function Sidebar({ section, items, activeKey, open, onSelect, onAdd, onRemove, sortMode, onSortModeChange, fetchUserProfile, myId }) {
  const [query, setQuery] = useState('');
  const [menuOpen, setMenuOpen] = useState(null);
  const [profileCache, setProfileCache] = useState({});
  const [profileLoading, setProfileLoading] = useState(null);

  // 点击菜单外部时关闭浮窗
  useEffect(() => {
    if (menuOpen === null) return;
    const close = () => setMenuOpen(null);
    document.addEventListener('click', close);
    return () => document.removeEventListener('click', close);
  }, [menuOpen]);

  const handleMoreClick = async (item, e) => {
    e.stopPropagation();
    const key = item.key;
    if (menuOpen === key) {
      setMenuOpen(null);
      return;
    }
    setMenuOpen(key);
    // 群聊不显示用户资料，只显示删除选项
    if (item.kind === 'group') return;
    // 如果已缓存，直接显示
    if (profileCache[key]) return;
    // 加载用户资料
    setProfileLoading(key);
    const prof = await fetchUserProfile(item.id);
    setProfileCache((prev) => ({ ...prev, [key]: prof || { error: true } }));
    setProfileLoading(null);
  };

  const list = section === 'chat'
    ? items.filter((item) => {
        if (!query) return true;
        const q = query.toLowerCase();
        return item.name.toLowerCase().includes(q) || String(item.id).includes(query);
      })
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
      {section === 'chat' && (
        <div className="sidebar-sort" role="group" aria-label="排序方式">
          <button
            type="button"
            className={sortMode === 'name' ? 'active' : ''}
            onClick={() => onSortModeChange('name')}
          >
            按名称
          </button>
          <button
            type="button"
            className={sortMode === 'active' ? 'active' : ''}
            onClick={() => onSortModeChange('active')}
          >
            按活跃
          </button>
        </div>
      )}
      <div className="sidebar-list">
        {list.length === 0 && (
          <div className="section-list-empty">
            这里暂时没有内容
            <br />
            点击右上角开始添加
          </div>
        )}
        {list.map((item) => (
          <div
            key={item.key}
            className={`section-list-item${item.kind === 'live' ? ' live' : ''}${item.active || activeKey === item.key ? ' active' : ''}`}
          >
            <span className="list-avatar" style={{ flexShrink: 0 }}>
              {item.avatarUrl ? (
                <img className="list-avatar-img" src={item.avatarUrl} alt={item.name || item.title} />
              ) : (
                initials(item.name || item.title)
              )}
            </span>
            <button
              type="button"
              className="section-list-copy-btn"
              onClick={() => onSelect(item)}
            >
              <span className="section-list-copy">
                <strong>{item.name || item.title}</strong>
                <small>{item.sub}</small>
              </span>
            </button>
            {item.unread > 0 ? (
              <span className="list-badge">{item.unread > 99 ? '99+' : item.unread}</span>
            ) : (
              <span className="list-time">{item.kind === 'live' ? (item.time || '') : formatActivityTime(item.lastActive)}</span>
            )}
            {onRemove && (
              <span className="list-more-wrap">
                <button
                  type="button"
                  className="list-more-btn"
                  aria-label="更多"
                  onClick={(e) => handleMoreClick(item, e)}
                >
                  <svg focusable="false" aria-hidden="true"><use href="#icon-more" /></svg>
                </button>
                {menuOpen === item.key && (
                  <div className="list-more-menu">
                    {/* 群聊：只显示删除/退出选项 */}
                    {item.kind === 'group' ? (
                      <button
                        type="button"
                        className="list-more-action list-more-danger"
                        onClick={(e) => {
                          e.stopPropagation();
                          setMenuOpen(null);
                          onRemove(item);
                        }}
                      >
                        退出群聊
                      </button>
                    ) : (
                      <>
                        {/* 用户资料卡片 */}
                        <div className="list-more-profile">
                          {profileLoading === item.key ? (
                            <div className="list-more-profile-loading">加载中...</div>
                          ) : profileCache[item.key] && !profileCache[item.key].error ? (
                            <>
                              <div className="list-more-profile-header">
                                {profileCache[item.key].avatarUrl ? (
                                  <img className="list-more-profile-avatar" src={profileCache[item.key].avatarUrl} alt={profileCache[item.key].nickname || profileCache[item.key].username} />
                                ) : (
                                  <span className="list-more-profile-avatar list-more-profile-avatar-fallback">
                                    {initials(profileCache[item.key].nickname || profileCache[item.key].username || item.name)}
                                  </span>
                                )}
                                <div className="list-more-profile-info">
                                  <div className="list-more-profile-name">{profileCache[item.key].nickname || profileCache[item.key].username || item.name}</div>
                                  <div className="list-more-profile-id">ID {profileCache[item.key].id || item.id}</div>
                                </div>
                              </div>
                              {profileCache[item.key].username && profileCache[item.key].nickname && (
                                <div className="list-more-profile-detail">用户名: {profileCache[item.key].username}</div>
                              )}
                            </>
                          ) : (
                            <div className="list-more-profile-header">
                              {item.avatarUrl ? (
                                <img className="list-more-profile-avatar" src={item.avatarUrl} alt={item.name} />
                              ) : (
                                <span className="list-more-profile-avatar list-more-profile-avatar-fallback">{initials(item.name)}</span>
                              )}
                              <div className="list-more-profile-info">
                                <div className="list-more-profile-name">{item.name}</div>
                                <div className="list-more-profile-id">ID {item.id}</div>
                              </div>
                            </div>
                          )}
                        </div>
                        {/* 删除好友选项 */}
                        <button
                          type="button"
                          className="list-more-action list-more-danger"
                          onClick={(e) => {
                            e.stopPropagation();
                            setMenuOpen(null);
                            onRemove(item);
                          }}
                        >
                          删除好友
                        </button>
                      </>
                    )}
                  </div>
                )}
              </span>
            )}
          </div>
        ))}
      </div>
    </aside>
  );
}
