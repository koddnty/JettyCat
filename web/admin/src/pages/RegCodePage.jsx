import React, { useState } from 'react';

export default function RegCodePage() {
  const [timeLimit, setTimeLimit] = useState(3600);
  const [validTimes, setValidTimes] = useState(1);
  const [code, setCode] = useState(null);
  const [msg, setMsg] = useState('注册码有效期和次数可按需设置。');
  const [busy, setBusy] = useState(false);

  const generate = async () => {
    setBusy(true);
    setMsg('');
    try {
      const params = new URLSearchParams({
        'time-limit': String(timeLimit || 3600),
        'valid-times': String(validTimes || 1),
      });
      const response = await fetch(`/api/registe/getRegCode?${params}`, { method: 'GET', credentials: 'include' });
      const text = await response.text();
      if (!response.ok) {
        let error = text || '生成失败';
        try {
          error = JSON.parse(text).error || error;
        } catch (err) {
          /* ignore */
        }
        setCode(null);
        setMsg(error);
      } else if (text.trim() === 'timeout') {
        setCode(null);
        setMsg('Redis 请求超时');
      } else {
        setCode(text.trim());
        setMsg('生成成功，请将注册码提供给用户。');
      }
    } catch (err) {
      setCode(null);
      setMsg('网络错误或服务器无法访问');
    } finally {
      setBusy(false);
    }
  };

  const reset = () => {
    setTimeLimit(3600);
    setValidTimes(1);
    setCode(null);
    setMsg('注册码有效期和次数可按需设置。');
  };

  const copy = async () => {
    try {
      await navigator.clipboard.writeText(code || '');
      setMsg('已复制到剪贴板');
    } catch (err) {
      setMsg('复制失败，请手动复制');
    }
  };

  return (
    <main className="panel">
      <p className="eyebrow">Registration access</p>
      <h2>生成注册码</h2>
      <p className="lead">为新用户生成一次性注册码。请求会携带当前 HttpOnly 认证 Cookie。</p>
      <div className="fields">
        <div className="field">
          <label htmlFor="timeLimit">过期时间（秒）</label>
          <input id="timeLimit" type="number" min="60" value={timeLimit} onChange={(e) => setTimeLimit(e.target.value)} />
        </div>
        <div className="field">
          <label htmlFor="validTimes">有效次数</label>
          <input id="validTimes" type="number" min="1" value={validTimes} onChange={(e) => setValidTimes(e.target.value)} />
        </div>
      </div>
      <div className="actions">
        <button className="button" type="button" disabled={busy} onClick={generate}>
          {busy ? '生成中...' : '生成注册码'}
        </button>
        <button className="button secondary" type="button" onClick={reset}>重置</button>
      </div>
      {code !== null && (
        <section className="result">
          <div>
            <div className="result-label">当前注册码</div>
            <div className="code">{code}</div>
          </div>
          <button className="button secondary" type="button" onClick={copy}>复制</button>
        </section>
      )}
      <div className="message" aria-live="polite">{msg}</div>
    </main>
  );
}
