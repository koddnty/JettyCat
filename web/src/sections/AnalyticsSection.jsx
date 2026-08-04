import React from 'react';

export default function AnalyticsSection() {
  return (
    <div className="module-page">
      <header className="module-heading">
        <div>
          <p className="eyebrow">Metrics</p>
          <h1>互动看板</h1>
          <p>把在线、消息和房间状态集中在一个轻量视图里。</p>
        </div>
        <span className="status">实时数据</span>
      </header>
      <section className="metrics-grid">
        <article>
          <small>当前在线</small>
          <strong>--</strong>
          <span>等待直播连接</span>
        </article>
        <article>
          <small>今日消息</small>
          <strong>--</strong>
          <span>由聊天模块统计</span>
        </article>
        <article>
          <small>互动峰值</small>
          <strong>--</strong>
          <span>暂无直播数据</span>
        </article>
      </section>
    </div>
  );
}
