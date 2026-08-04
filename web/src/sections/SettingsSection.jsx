import React, { useState } from 'react';

export default function SettingsSection() {
  const [notify, setNotify] = useState(true);
  const [compact, setCompact] = useState(false);

  return (
    <div className="module-page">
      <header className="module-heading">
        <div>
          <p className="eyebrow">Preferences</p>
          <h1>设置</h1>
          <p>管理通知、工作台偏好和当前连接状态。</p>
        </div>
      </header>
      <section className="settings-list">
        <label>
          <span>
            <strong>新消息顶部提醒</strong>
            <small>收到新私聊时在工作台右上角弹出气泡</small>
          </span>
          <input type="checkbox" checked={notify} onChange={(e) => setNotify(e.target.checked)} />
        </label>
        <label>
          <span>
            <strong>进入工作台自动连接</strong>
            <small>登录后立即建立 WebSocket 长连接</small>
          </span>
          <input type="checkbox" checked disabled />
        </label>
        <label>
          <span>
            <strong>紧凑列表</strong>
            <small>减少左侧列表的间距，显示更多会话</small>
          </span>
          <input type="checkbox" checked={compact} onChange={(e) => setCompact(e.target.checked)} />
        </label>
      </section>
    </div>
  );
}
