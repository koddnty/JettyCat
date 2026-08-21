import React, { useState } from 'react';
import { useChat } from '../ws';

export default function ContactsSection({ onOpenChat }) {
  const { conversations, contactsReady, friendRequests, addFriend, removeFriend, addGroup, removeGroup, agreeFriend, rejectFriendRequest, showNotice } = useChat();
  const [open, setOpen] = useState(false);
  const [mode, setMode] = useState('friend');
  const [id, setId] = useState('');
  const [busy, setBusy] = useState(false);
  const [busyReq, setBusyReq] = useState(null);
  const [confirmRejectId, setConfirmRejectId] = useState(null);

  const friends = conversations.filter((item) => item.kind === 'user');
  const groups = conversations.filter((item) => item.kind === 'group');

  const openDialog = (nextMode) => {
    setMode(nextMode);
    setId('');
    setOpen(true);
  };

  const save = async () => {
    const raw = String(id || '').trim();
    if (mode === 'group' && !/^\d+$/.test(raw)) return;
    if (mode === 'friend' && !/^[A-Za-z0-9]+$/.test(raw)) return;
    setBusy(true);
    try {
      const value = mode === 'group' ? Number(raw) : raw;
      const result = mode === 'group' ? await addGroup(value) : await addFriend(value);
      if (!result || result.code !== 200) {
        showNotice(mode === 'group' ? '加入群聊' : '添加好友', (result && result.msg) || '操作失败, 请稍后重试');
        return;
      }
      setOpen(false);
      setId('');
      if (mode === 'group') {
        onOpenChat({
          kind: 'group',
          id: String((result && result.data && result.data.group_snow_id) || value),
          name: (result && result.data && result.data.group_name) || `群组 ${value}`,
        });
      } else if (result && result.msg) {
        showNotice('好友申请已发送', result.msg);
      }
    } finally {
      setBusy(false);
    }
  };

  // 同意某人的好友申请
  const agree = async (item) => {
    setConfirmRejectId(null);
    setBusyReq(item.id);
    try {
      const result = await agreeFriend(item.username || item.id);
      if (!result || result.code !== 200) {
        showNotice('同意好友申请', (result && result.msg) || '操作失败, 请稍后重试');
      } else {
        showNotice('好友申请已通过', `${item.nickname || item.username} 已成为你的好友`);
      }
    } finally {
      setBusyReq(null);
    }
  };

  // 拒绝/忽略某人的好友申请 (两段式确认, 避免误触)
  const reject = async (item) => {
    if (confirmRejectId !== item.id) {
      setConfirmRejectId(item.id);
      return;
    }
    setConfirmRejectId(null);
    setBusyReq(item.id);
    try {
      const result = await rejectFriendRequest(item.username || item.id);
      if (!result || result.code !== 200) {
        showNotice('忽略好友申请', (result && result.msg) || '操作失败, 请稍后重试');
      } else {
        showNotice('已忽略好友申请', `已忽略来自「${item.nickname || item.username}」的好友申请`);
      }
    } finally {
      setBusyReq(null);
    }
  };

  const remove = async (item) => {
    const label = item.kind === 'group' ? `退出群聊「${item.name}」?` : `删除好友「${item.name}」?`;
    if (!window.confirm(label)) return;
    const result = item.kind === 'group' ? await removeGroup(item.groupId) : await removeFriend(item.username);
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
        <div className="module-card-icon">✉</div>
        <div>
          <h2>好友申请</h2>
          <p>{friendRequests.length > 0 ? `${friendRequests.length} 条待处理的申请` : '暂无待处理的好友申请'}</p>
        </div>
      </section>

      {friendRequests.length > 0 && (
        <ul className="contact-list">
          {friendRequests.map((item) => (
            <li key={`req:${item.id}`} className="contact-item">
              <span className="contact-avatar">
                {item.avatarUrl ? (
                  <img className="contact-avatar-img" src={item.avatarUrl} alt={item.nickname || item.username} />
                ) : (
                  initials(item.nickname || item.username)
                )}
              </span>
              <span className="contact-copy">
                <strong>{item.nickname || item.username}</strong>
                <small>{item.username ? `用户名 ${item.username}` : '请求添加你为好友'}</small>
              </span>
              <button className="contact-open" type="button" disabled={busyReq === item.id} onClick={() => agree(item)}>
                {busyReq === item.id ? '处理中...' : '同意'}
              </button>
              <button className="contact-remove" type="button" disabled={busyReq === item.id} onClick={() => reject(item)}>
                {confirmRejectId === item.id ? '确认忽略?' : '拒绝'}
              </button>
            </li>
          ))}
        </ul>
      )}

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
              <span className="contact-avatar">
                {item.avatarUrl ? (
                  <img className="contact-avatar-img" src={item.avatarUrl} alt={item.name} />
                ) : (
                  initials(item.name)
                )}
              </span>
              <span className="contact-copy">
                <strong>{item.name}</strong>
                <small>{item.username ? `用户名 ${item.username}` : '好友'}</small>
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
              <span className="contact-avatar">
                {item.avatarUrl ? (
                  <img className="contact-avatar-img" src={item.avatarUrl} alt={item.name} />
                ) : (
                  initials(item.name)
                )}
              </span>
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
            {mode === 'group' ? '群聊 ID' : '用户名(账号)'}
            <input type="text" inputMode={mode === 'group' ? 'numeric' : 'text'} placeholder={mode === 'group' ? '例如 1' : '例如 tom'} value={id} onChange={(e) => setId(e.target.value)} autoFocus />
          </label>
          <div className="dialog-actions">
            <button className="button secondary" type="button" onClick={() => setOpen(false)}>取消</button>
            <button className="button" type="button" onClick={save} disabled={busy}>
              {busy ? '处理中...' : mode === 'group' ? '加入' : '发送申请'}
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
