import React, { useState } from 'react';
import { useChat } from '../ws';

export default function ContactsSection({ onOpenChat }) {
  const { conversations, contactsReady, addFriend, removeFriend, addGroup, removeGroup, showNotice } = useChat();
  const [open, setOpen] = useState(false);
  const [mode, setMode] = useState('friend');
  const [id, setId] = useState('');
  const [busy, setBusy] = useState(false);

  const friends = conversations.filter((item) => item.kind === 'user');
  const groups = conversations.filter((item) => item.kind === 'group');

  const openDialog = (nextMode) => {
    setMode(nextMode);
    setId('');
    setOpen(true);
  };

  const save = async () => {
    const value = Number(id);
    if (!Number.isInteger(value) || value < 1) return;
    setBusy(true);
    try {
      const result = mode === 'group' ? await addGroup(value) : await addFriend(value);
      if (!result || result.code !== 200) {
        showNotice(mode === 'group' ? '加入群聊' : '添加好友', (result && result.msg) || '操作失败, 请稍后重试');
        return;
      }
      setOpen(false);
      setId('');
      if (mode === 'group') {
        onOpenChat({ kind: 'group', id: value });
      }
    } finally {
      setBusy(false);
    }
  };

  const remove = async (item) => {
    const label = item.kind === 'group' ? `退出群聊「${item.name}」?` : `删除好友「${item.name}」?`;
    if (!window.confirm(label)) return;
    const result = item.kind === 'group' ? await removeGroup(item.id) : await removeFriend(item.id);
    if (!result || result.code !== 200) {
      showNotice(item.kind === 'group' ? '退出群聊' : '删除好友', (result && result.msg) || '操作失败, 请稍后重试');
    }
  };

  return (
    <div className="module-page">
      <header className="module-heading">
        <div>
          <p className="eyebrow">Contacts</p>
          <h1>联系人</h1>
          <p>好友与群聊列表来自服务端, 每次打开页面都会同步最新关系。</p>
        </div>
        <div className="heading-actions">
          <button className="button" type="button" onClick={() => openDialog('friend')}>＋ 添加好友</button>
          <button className="button" type="button" onClick={() => openDialog('group')}>＋ 加入群聊</button>
        </div>
      </header>

      <section className="module-card">
        <div className="module-card-icon">♙</div>
        <div>
          <h2>好友列表</h2>
          <p>{contactsReady ? `${friends.length} 位好友` : '正在从服务端同步...'}</p>
        </div>
      </section>

      {friends.length > 0 && (
        <ul className="contact-list">
          {friends.map((item) => (
            <li key={`user:${item.id}`} className="contact-item">
              <span className="contact-avatar">{initials(item.name)}</span>
              <span className="contact-copy">
                <strong>{item.name}</strong>
                <small>ID {item.id}</small>
              </span>
              <button className="contact-open" type="button" onClick={() => onOpenChat(item)}>发消息</button>
              <button className="contact-remove" type="button" onClick={() => remove(item)}>删除</button>
            </li>
          ))}
        </ul>
      )}

      <section className="module-card">
        <div className="module-card-icon">☰</div>
        <div>
          <h2>群聊列表</h2>
          <p>{contactsReady ? `${groups.length} 个群聊` : '正在从服务端同步...'}</p>
        </div>
      </section>

      {groups.length > 0 && (
        <ul className="contact-list">
          {groups.map((item) => (
            <li key={`group:${item.id}`} className="contact-item">
              <span className="contact-avatar">{initials(item.name)}</span>
              <span className="contact-copy">
                <strong>{item.name}</strong>
                <small>ID {item.id}</small>
              </span>
              <button className="contact-open" type="button" onClick={() => onOpenChat(item)}>进入群聊</button>
              <button className="contact-remove" type="button" onClick={() => remove(item)}>退出</button>
            </li>
          ))}
        </ul>
      )}

      <div className="module-dialog" hidden={!open}>
        <div className="module-dialog-backdrop" onClick={() => setOpen(false)} />
        <div className="module-dialog-card">
          <p className="eyebrow">New contact</p>
          <h2>{mode === 'group' ? '加入群聊' : '添加好友'}</h2>
          <label>
            {mode === 'group' ? '群聊 ID' : '用户 ID'}
            <input type="number" min="1" placeholder={mode === 'group' ? '例如 1' : '例如 20'} value={id} onChange={(e) => setId(e.target.value)} autoFocus />
          </label>
          <div className="dialog-actions">
            <button className="button secondary" type="button" onClick={() => setOpen(false)}>取消</button>
            <button className="button" type="button" onClick={save} disabled={busy}>
              {busy ? '处理中...' : mode === 'group' ? '加入' : '添加'}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}

function initials(name) {
  return String(name || '?').trim().slice(0, 2).toUpperCase();
}
