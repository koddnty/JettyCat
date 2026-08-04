import React, { useState } from 'react';
import { useChat } from '../ws';

export default function ContactsSection({ onOpenChat }) {
  const { addConversation } = useChat();
  const [open, setOpen] = useState(false);
  const [id, setId] = useState('');

  const save = () => {
    const value = Number(id);
    if (!Number.isInteger(value) || value < 1) return;
    addConversation('user', value);
    setOpen(false);
    setId('');
    onOpenChat({ kind: 'user', id: value });
  };

  return (
    <div className="module-page">
      <header className="module-heading">
        <div>
          <p className="eyebrow">Contacts</p>
          <h1>联系人</h1>
          <p>通过用户 ID 快速建立私聊，也可以管理常用联系人。</p>
        </div>
        <button className="button" type="button" onClick={() => setOpen(true)}>＋ 添加联系人</button>
      </header>

      <section className="module-card">
        <div className="module-card-icon">♙</div>
        <div>
          <h2>还没有联系人分组</h2>
          <p>添加一个用户 ID 后，聊天会话会同步出现在左侧聊天列表。</p>
        </div>
        <button className="button" type="button" onClick={() => setOpen(true)}>开始添加</button>
      </section>

      <div className="module-dialog" hidden={!open}>
        <div className="module-dialog-card">
          <button className="dialog-close" type="button" aria-label="关闭" onClick={() => setOpen(false)}>
            <svg className="icon" focusable="false" aria-hidden="true"><use href="#icon-close" /></svg>
          </button>
          <p className="eyebrow">New contact</p>
          <h2>添加联系人</h2>
          <label>
            用户 ID
            <input type="number" min="1" placeholder="例如 20" value={id} onChange={(e) => setId(e.target.value)} autoFocus />
          </label>
          <div className="dialog-actions">
            <button className="button secondary" type="button" onClick={() => setOpen(false)}>取消</button>
            <button className="button" type="button" onClick={save}>添加</button>
          </div>
        </div>
      </div>
    </div>
  );
}
