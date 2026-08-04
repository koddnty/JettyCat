import React from 'react';

export default function LiveSection() {
  return (
    <div className="module-page">
      <header className="module-heading">
        <div>
          <p className="eyebrow">Live room</p>
          <h1>直播控制</h1>
          <p>管理房间状态、推流信息和实时互动。</p>
        </div>
        <span className="status">服务运行正常</span>
      </header>
      <div className="module-grid">
        <article className="module-card">
          <div className="module-card-icon">◉</div>
          <div>
            <h2>我的直播间</h2>
            <p>当前没有正在进行的直播，准备好后可以从这里开始。</p>
          </div>
          <button className="button" type="button">创建房间</button>
        </article>
        <article className="module-card">
          <div className="module-card-icon">↗</div>
          <div>
            <h2>房间状态</h2>
            <p>在线人数、消息流量和互动峰值将在直播开始后显示。</p>
          </div>
        </article>
      </div>
    </div>
  );
}
