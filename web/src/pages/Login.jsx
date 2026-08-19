import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';

const API_URL = '/api/login/jwt';
const REGISTER_URL = '/api/registe/registe';

async function requestJson(url, params, method = 'GET') {
  const response = await fetch(url, {
    method,
    credentials: 'include',
    headers: { 'Content-Type': 'application/json' },
    body: method === 'POST' ? JSON.stringify(params) : undefined,
  });
  const text = await response.text();
  try {
    return JSON.parse(text);
  } catch (error) {
    throw new Error('服务器响应格式错误，请稍后重试');
  }
}

function Field({ label, type = 'text', value, onChange, placeholder, autoComplete, children }) {
  const [show, setShow] = useState(false);
  const resolvedType = type === 'password' && show ? 'text' : type;
  return (
    <div className="field">
      <label>{label}</label>
      <div className={type === 'password' ? 'password-wrap' : undefined}>
        <input
          type={resolvedType}
          required
          autoComplete={autoComplete}
          placeholder={placeholder}
          value={value}
          onChange={(e) => onChange(e.target.value)}
        />
        {type === 'password' && (
          <button className="eye-button" type="button" onClick={() => setShow((v) => !v)}>
            {show ? '隐藏' : '显示'}
          </button>
        )}
      </div>
      {children}
    </div>
  );
}

export default function LoginPage() {
  const navigate = useNavigate();
  const [mode, setMode] = useState('login');
  const [alert, setAlert] = useState(null);
  const [busy, setBusy] = useState(false);

  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [regUsername, setRegUsername] = useState('');
  const [regNickname, setRegNickname] = useState('');
  const [regPassword, setRegPassword] = useState('');
  const [regPasswordConfirm, setRegPasswordConfirm] = useState('');
  const [regCode, setRegCode] = useState('');

  const showAlert = (message, type = 'error') => {
    setAlert({ message, type });
    if (type === 'error') window.setTimeout(() => setAlert(null), 5000);
  };

  const switchMode = (next) => {
    setMode(next);
    setAlert(null);
  };

  const validateLogin = () => {
    if (!username.trim() || !password) return showAlert('请输入用户名和密码'), false;
    if (!/^[A-Za-z0-9]+$/.test(username.trim())) return showAlert('用户名只能包含英文字母和数字'), false;
    if (username.trim().length < 3) return showAlert('用户名至少 3 个字符'), false;
    if (password.length < 6) return showAlert('密码至少 6 个字符'), false;
    return true;
  };

  const validateRegister = () => {
    if (!regUsername.trim() || !regNickname.trim() || !regPassword || !regPasswordConfirm || !regCode.trim())
      return showAlert('请完整填写注册信息'), false;
    if (!/^[A-Za-z0-9]+$/.test(regUsername.trim())) return showAlert('用户名只能包含英文字母和数字'), false;
    if (regUsername.trim().length < 3) return showAlert('用户名至少 3 个字符'), false;
    if (regPassword.length < 6) return showAlert('密码至少 6 个字符'), false;
    if (regPassword !== regPasswordConfirm) return showAlert('两次输入的密码不一致'), false;
    return true;
  };

  const submitLogin = async (event) => {
    event.preventDefault();
    if (!validateLogin()) return;
    setBusy(true);
    try {
      const data = await requestJson(API_URL, { username: username.trim(), password }, 'POST');
      if (data.status !== 'success') throw new Error(data.error || '登录失败，请检查用户名和密码');
      showAlert('登录成功，正在进入控制台...', 'success');
      window.setTimeout(() => navigate('/main'), 700);
    } catch (error) {
      showAlert(error.message || '网络连接失败，请检查服务器是否运行');
    } finally {
      setBusy(false);
    }
  };

  const submitRegister = async (event) => {
    event.preventDefault();
    if (!validateRegister()) return;
    setBusy(true);
    try {
      const data = await requestJson(REGISTER_URL, {
        username: regUsername.trim(),
        nickname: regNickname.trim(),
        password: regPassword,
        reg_code: regCode.trim(),
      }, 'POST');
      if (data.status !== 'success') throw new Error(data.error || '注册失败，请检查输入内容');
      showAlert('注册成功，正在进入控制台...', 'success');
      window.setTimeout(() => navigate('/main'), 700);
    } catch (error) {
      showAlert(error.message || '网络连接失败，请检查服务器是否运行');
    } finally {
      setBusy(false);
    }
  };

  return (
    <main className="auth-page">
      <div className="auth-shell">
        <section className="auth-intro">
          <a className="brand" href="/">
            <span className="brand-mark">
              <svg className="icon" focusable="false" aria-hidden="true">
                <use href="#icon-logo" />
              </svg>
            </span>
            <span>jettyCat</span>
          </a>
          <div>
            <p className="eyebrow">Live interaction studio</p>
            <h1>清晰、实时地连接每一次互动。</h1>
          </div>
          <p className="intro-note">实时互动工作台</p>
        </section>
        <section className="auth-card">
          <p className="eyebrow">Welcome back</p>
          <h2>{mode === 'login' ? '进入工作台' : '创建账户'}</h2>
          {alert && <div className={`alert show alert-${alert.type}`} role="alert">{alert.message}</div>}
          <div className="mode-switch">
            <button className={`mode-btn${mode === 'login' ? ' active' : ''}`} type="button" onClick={() => switchMode('login')}>
              登录
            </button>
            <button className={`mode-btn${mode === 'register' ? ' active' : ''}`} type="button" onClick={() => switchMode('register')}>
              注册
            </button>
          </div>

          <form className={`form-panel${mode === 'login' ? ' active' : ''}`} onSubmit={submitLogin}>
            <Field label="用户名" value={username} onChange={setUsername} placeholder="请输入用户名" autoComplete="username" />
            <Field
              label="密码"
              type="password"
              value={password}
              onChange={setPassword}
              placeholder="请输入密码"
              autoComplete="current-password"
            />
            <button className="button form-submit" type="submit" disabled={busy}>
              {busy ? '登录中...' : '登录'}
            </button>
          </form>

          <form className={`form-panel${mode === 'register' ? ' active' : ''}`} onSubmit={submitRegister}>
            <Field label="用户名" value={regUsername} onChange={setRegUsername} placeholder="仅限英文字母和数字" autoComplete="username" />
            <Field label="昵称" value={regNickname} onChange={setRegNickname} placeholder="展示给其他用户的昵称" autoComplete="nickname" />
            <Field
              label="密码"
              type="password"
              value={regPassword}
              onChange={setRegPassword}
              placeholder="至少 6 个字符"
              autoComplete="new-password"
            />
            <Field
              label="确认密码"
              type="password"
              value={regPasswordConfirm}
              onChange={setRegPasswordConfirm}
              placeholder="再次输入密码"
              autoComplete="new-password"
            />
            <Field label="注册验证码" value={regCode} onChange={setRegCode} placeholder="请输入管理员提供的验证码" autoComplete="one-time-code">
              <div className="helper">注册码由管理员在后台生成。</div>
            </Field>
            <button className="button form-submit" type="submit" disabled={busy}>
              {busy ? '注册中...' : '注册并进入'}
            </button>
          </form>

          <div className="auth-footer">
            使用管理员账号？<a href="/admin/">进入管理后台</a>
          </div>
        </section>
      </div>
    </main>
  );
}
