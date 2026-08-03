const API_URL = '/api/login/jwt';
const REGISTER_URL = '/api/registe/registe';

const $ = (id) => document.getElementById(id);
const loginForm = $('loginForm');
const registerForm = $('registerForm');
const alertBox = $('alertBox');

function showAlert(message, type = 'error') {
  alertBox.textContent = message;
  alertBox.className = `alert show alert-${type}`;
  if (type === 'error') window.setTimeout(() => alertBox.classList.remove('show'), 5000);
}

function setMode(mode) {
  const login = mode === 'login';
  loginForm.classList.toggle('active', login);
  registerForm.classList.toggle('active', !login);
  $('loginModeBtn').classList.toggle('active', login);
  $('registerModeBtn').classList.toggle('active', !login);
  alertBox.classList.remove('show');
  $(login ? 'username' : 'regUsername').focus();
}

function togglePassword(id, button) {
  const input = $(id);
  input.type = input.type === 'password' ? 'text' : 'password';
  button.textContent = input.type === 'password' ? '显示' : '隐藏';
}

function validateLogin() {
  const username = $('username').value.trim();
  const password = $('password').value;
  if (!username || !password) return showAlert('请输入用户名和密码'), false;
  if (username.length < 3) return showAlert('用户名至少 3 个字符'), false;
  if (password.length < 6) return showAlert('密码至少 6 个字符'), false;
  return true;
}

function validateRegister() {
  const username = $('regUsername').value.trim();
  const password = $('regPassword').value;
  const confirmation = $('regPasswordConfirm').value;
  const code = $('regCode').value.trim();
  if (!username || !password || !confirmation || !code) return showAlert('请完整填写注册信息'), false;
  if (username.length < 3) return showAlert('用户名至少 3 个字符'), false;
  if (password.length < 6) return showAlert('密码至少 6 个字符'), false;
  if (password !== confirmation) return showAlert('两次输入的密码不一致'), false;
  return true;
}

async function requestJson(url, params) {
  const response = await fetch(`${url}?${new URLSearchParams(params)}`, { method: 'GET', credentials: 'include' });
  const text = await response.text();
  try { return JSON.parse(text); } catch { throw new Error('服务器响应格式错误，请稍后重试'); }
}

async function submitLogin(event) {
  event.preventDefault();
  if (!validateLogin()) return;
  const button = $('loginBtn');
  button.disabled = true;
  button.textContent = '登录中...';
  try {
    const data = await requestJson(API_URL, { username: $('username').value.trim(), password: $('password').value });
    if (data.status !== 'success') throw new Error(data.error || '登录失败，请检查用户名和密码');
    showAlert('登录成功，正在进入控制台...', 'success');
    window.setTimeout(() => { window.location.href = '/pages/main.html'; }, 700);
  } catch (error) { showAlert(error.message || '网络连接失败，请检查服务器是否运行'); }
  finally { button.disabled = false; button.textContent = '登录'; }
}

async function submitRegister(event) {
  event.preventDefault();
  if (!validateRegister()) return;
  const button = $('registerBtn');
  button.disabled = true;
  button.textContent = '注册中...';
  try {
    const data = await requestJson(REGISTER_URL, { username: $('regUsername').value.trim(), password: $('regPassword').value, reg_code: $('regCode').value.trim() });
    if (data.status !== 'success') throw new Error(data.error || '注册失败，请检查输入内容');
    showAlert('注册成功，正在进入控制台...', 'success');
    window.setTimeout(() => { window.location.href = '/pages/main.html'; }, 700);
  } catch (error) { showAlert(error.message || '网络连接失败，请检查服务器是否运行'); }
  finally { button.disabled = false; button.textContent = '注册并进入'; }
}

loginForm.addEventListener('submit', submitLogin);
registerForm.addEventListener('submit', submitRegister);
$('loginModeBtn').addEventListener('click', () => setMode('login'));
$('registerModeBtn').addEventListener('click', () => setMode('register'));
$('regPasswordToggle').addEventListener('click', (event) => togglePassword('regPassword', event.currentTarget));
$('regPasswordConfirmToggle').addEventListener('click', (event) => togglePassword('regPasswordConfirm', event.currentTarget));
$('loginPasswordToggle').addEventListener('click', (event) => togglePassword('password', event.currentTarget));
