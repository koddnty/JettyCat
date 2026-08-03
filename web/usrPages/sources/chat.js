(function () {
  'use strict';

  const WS_PATH = '/api/chat';
  const PAGE_SIZE = 15;
  const STORE_KEY = 'jettycat.chat.conversations';
  const $ = (id) => document.getElementById(id);

  const REST = {
    user: '/api/chat/fetch_user_message',
    group: '/api/chat/fetch_group_message',
  };

  const state = {
    ws: null,
    connected: false,
    myId: null,
    myIdWaiters: [],
    conversations: loadStore(),
    activeKey: null,
    messages: new Map(),
    offsets: new Map(),
    loading: false,
    hasMore: true,
    modalKind: 'user',
    reconnect: 0,
    authFailed: false,
  };

  /* ---------- storage ---------- */

  function loadStore() {
    try {
      const raw = JSON.parse(localStorage.getItem(STORE_KEY) || '[]');
      if (!Array.isArray(raw)) return [];
      return raw
        .filter((c) => c && (c.kind === 'user' || c.kind === 'group') && Number.isInteger(Number(c.id)))
        .map((c) => ({ kind: c.kind, id: Number(c.id) }));
    } catch (e) { return []; }
  }

  function saveStore() {
    try { localStorage.setItem(STORE_KEY, JSON.stringify(state.conversations)); }
    catch (e) { /* ignore */ }
  }

  const keyOf = (kind, id) => kind + ':' + id;

  /* ---------- helpers ---------- */

  function convName(conv) {
    return conv.kind === 'user' ? '用户 ' + conv.id : '群组 ' + conv.id;
  }

  function fmtTime(ts, withDate) {
    const d = new Date(ts * 1000);
    const pad = (n) => String(n).padStart(2, '0');
    const hm = pad(d.getHours()) + ':' + pad(d.getMinutes());
    if (!withDate) return hm;
    return (d.getMonth() + 1) + '月' + d.getDate() + '日 ' + hm;
  }

  function dayLabel(ts) {
    const d = new Date(ts * 1000);
    const now = new Date();
    const start = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
    if (ts * 1000 >= start) return '今天';
    if (ts * 1000 >= start - 86400000) return '昨天';
    return (d.getMonth() + 1) + '月' + d.getDate() + '日';
  }

  const sameDay = (a, b) => {
    const x = new Date(a * 1000), y = new Date(b * 1000);
    return x.getFullYear() === y.getFullYear() && x.getMonth() === y.getMonth() && x.getDate() === y.getDate();
  };

  function bubbleText(m) {
    if (m.type === 'TEXT') return m.content;
    return '[' + m.type + '] ' + (m.content || '');
  }

  function showToast(text) {
    const el = $('toast');
    el.textContent = text;
    el.classList.add('show');
    clearTimeout(showToast._t);
    showToast._t = setTimeout(() => el.classList.remove('show'), 2600);
  }

  /* ---------- WebSocket ---------- */

  function setConn(status) {
    const badge = $('connBadge');
    const map = {
      online: ['已连接', 'online'],
      connecting: ['连接中…', 'connecting'],
      offline: ['连接断开', 'offline'],
    };
    badge.className = 'conn-badge ' + map[status][1];
    badge.textContent = map[status][0];
    updateInputState();
  }

  function connectWS() {
    setConn('connecting');
    const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
    const ws = new WebSocket(proto + location.host + WS_PATH);
    state.ws = ws;

    ws.onopen = () => {
      state.connected = true;
      state.reconnect = 0;
      setConn('online');
    };

    ws.onmessage = (ev) => handleWsMessage(ev.data);

    ws.onclose = (ev) => {
      state.connected = false;
      if (ev.code === 1008) {
        handleAuthFailure();
        return;
      }
      setConn('offline');
      scheduleReconnect();
    };

    ws.onerror = () => { try { ws.close(); } catch (e) { /* ignore */ } };
  }

  function scheduleReconnect() {
    clearTimeout(connectWS._t);
    state.reconnect += 1;
    const delay = Math.min(30000, 2500 * Math.pow(1.6, state.reconnect - 1));
    connectWS._t = setTimeout(connectWS, delay);
  }

  function waitForMyId(timeout) {
    if (state.myId !== null) return Promise.resolve(state.myId);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const i = state.myIdWaiters.indexOf(handler);
        if (i >= 0) state.myIdWaiters.splice(i, 1);
        reject(new Error('连接未就绪，无法加载历史'));
      }, timeout || 8000);
      function handler(id) { clearTimeout(timer); resolve(id); }
      state.myIdWaiters.push(handler);
    });
  }

  function handleWsMessage(raw) {
    let msg;
    try { msg = JSON.parse(raw); } catch (e) { return; }

    if (msg.code === 401) {
      handleAuthFailure();
      return;
    }

    // 欢迎消息 from=0, to=当前用户id → 用于识别自己的用户id
    if (msg.from === 0 && msg.to && state.myId === null) {
      state.myId = msg.to;
      const waiters = state.myIdWaiters;
      state.myIdWaiters = [];
      waiters.forEach((fn) => fn(state.myId));
      refreshPeerTitle();
      updateInputState();
      return;
    }

    if (msg.type !== 'private_message') return;

    const inner = parseInnerMessage(msg.content);
    if (!inner) return;
    const senderId = (inner.from !== undefined && inner.from !== null) ? inner.from : msg.from;
    const partnerId = senderId === state.myId ? msg.to : senderId;
    if (partnerId === undefined || partnerId === null) return;

    ensureConversation('user', partnerId);
    const key = keyOf('user', partnerId);
    const entry = {
      date: inner.date || Math.floor(Date.now() / 1000),
      from: senderId,
      type: inner.type || 'TEXT',
      content: inner.content,
    };
    const isActive = key === state.activeKey;
    if (!isActive) {
      const conv = state.conversations.find((c) => keyOf(c.kind, c.id) === key);
      if (conv) conv._unread = (conv._unread || 0) + 1;
    }
    appendMessage(key, entry);
    if (isActive) scrollToBottom();
  }

  function parseInnerMessage(content) {
    if (typeof content !== 'string') return null;
    try { return JSON.parse(content); } catch (e) { return null; }
  }

  function handleAuthFailure() {
    if (state.authFailed) return;
    state.authFailed = true;
    setConn('offline');
    const sub = $('peerSub');
    if (sub) sub.textContent = '登录状态失效，请重新登录';
    showToast('登录已失效，请重新登录');
    setTimeout(() => { window.location.href = '/pages/login.html'; }, 1800);
  }

  function ensureConversation(kind, id) {
    const exists = state.conversations.some((c) => c.kind === kind && c.id === id);
    if (!exists) {
      state.conversations.push({ kind, id });
      saveStore();
    }
  }

  /* ---------- rendering ---------- */

  function renderConversations() {
    const listEl = $('convList');
    listEl.innerHTML = '';
    if (!state.conversations.length) {
      listEl.innerHTML = '<div class="conv-empty">还没有会话<br>点击「新建会话」开始聊天</div>';
      return;
    }
    state.conversations.forEach((conv) => {
      const key = keyOf(conv.kind, conv.id);
      const msgs = state.messages.get(key) || [];
      const last = msgs.length ? msgs[msgs.length - 1] : null;
      const unread = conv._unread || 0;
      const item = document.createElement('button');
      item.type = 'button';
      item.className = 'conv-item kind-' + conv.kind + (key === state.activeKey ? ' active' : '') + (unread ? ' unread' : '');
      const avatar = document.createElement('span');
      avatar.className = 'conv-avatar';
      avatar.textContent = conv.kind === 'user' ? ('U' + conv.id).slice(-2) : 'G' + conv.id;
      const main = document.createElement('span');
      main.className = 'conv-main';
      const title = document.createElement('span');
      title.className = 'conv-title';
      title.innerHTML = '<strong>' + escapeHtml(convName(conv)) + '</strong><span class="conv-time">' + (last ? fmtTime(last.date) : '') + '</span>';
      const preview = document.createElement('span');
      preview.className = 'conv-preview';
      preview.innerHTML = '<span>' + (last ? escapeHtml(bubbleText(last)) : '暂无消息') + '</span><span class="conv-badge">' + unread + '</span>';
      main.appendChild(title);
      main.appendChild(preview);
      item.appendChild(avatar);
      item.appendChild(main);
      item.addEventListener('click', () => openConversation(conv));
      listEl.appendChild(item);
    });
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, (ch) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));
  }

  function renderMessages(key) {
    const listEl = $('msgList');
    listEl.innerHTML = '';
    const conv = activeConv();
    const msgs = state.messages.get(key) || [];
    if (!msgs.length) {
      listEl.innerHTML = '<div class="msg-empty">开始一段对话吧</div>';
      return;
    }
    msgs.forEach((m, i) => {
      if (i === 0 || !sameDay(msgs[i - 1].date, m.date)) {
        const sep = document.createElement('div');
        sep.className = 'date-sep';
        sep.textContent = dayLabel(m.date);
        listEl.appendChild(sep);
      }
      const mine = state.myId !== null && m.from === state.myId;
      const row = document.createElement('div');
      row.className = 'msg-row' + (mine ? ' self' : '') + ' kind-' + (conv.kind);
      const avatar = document.createElement('span');
      avatar.className = 'msg-avatar';
      avatar.textContent = mine ? '我' : (conv.kind === 'group' ? ('U' + m.from).slice(-2) : '他');
      const body = document.createElement('div');
      body.className = 'msg-body';
      if (conv.kind === 'group' && !mine) {
        const sender = document.createElement('div');
        sender.className = 'msg-sender';
        sender.textContent = '用户 ' + m.from;
        body.appendChild(sender);
      }
      const bubble = document.createElement('div');
      bubble.className = 'bubble' + (m.type === 'TEXT' ? '' : ' ' + m.type.toLowerCase());
      bubble.textContent = bubbleText(m);
      body.appendChild(bubble);
      const time = document.createElement('div');
      time.className = 'msg-time';
      time.textContent = fmtTime(m.date, true);
      body.appendChild(time);
      row.appendChild(avatar);
      row.appendChild(body);
      listEl.appendChild(row);
    });
  }

  function refreshPeerTitle() {
    const conv = activeConv();
    const titleEl = $('peerTitle');
    const subEl = $('peerSub');
    const avatar = $('peerAvatar');
    if (!conv) {
      titleEl.textContent = '选择一个会话';
      subEl.textContent = '从左侧选择或新建一个会话';
      avatar.textContent = '?';
      return;
    }
    $('peerBar').classList.toggle('kind-group', conv.kind === 'group');
    avatar.textContent = conv.kind === 'user' ? ('U' + conv.id).slice(-2) : 'G' + conv.id;
    titleEl.textContent = convName(conv);
    subEl.textContent = conv.kind === 'group'
      ? (state.connected ? '群消息暂不支持实时收发，可加载历史记录' : '连接已断开')
      : (state.connected
        ? '我的 ID：' + (state.myId === null ? '…' : state.myId) + ' · 实时连接已就绪'
        : '连接已断开，正在重连…');
  }

  /* ---------- messages ---------- */

  function appendMessage(key, entry) {
    const list = state.messages.get(key) || [];
    const dup = list.some((m) => m.date === entry.date && m.from === entry.from && m.content === entry.content);
    if (dup) return;
    list.push(entry);
    list.sort((a, b) => a.date - b.date);
    state.messages.set(key, list);
    if (key === state.activeKey) renderMessages(key);
    renderConversations();
  }

  function prependHistory(key, entries) {
    let list = state.messages.get(key) || [];
    const seen = new Set(list.map((m) => m.date + '|' + m.from + '|' + m.content));
    const fresh = [];
    entries.forEach((m) => {
      const sig = m.date + '|' + m.from + '|' + m.content;
      if (!seen.has(sig)) { seen.add(sig); fresh.push(m); }
    });
    list = fresh.concat(list);
    list.sort((a, b) => a.date - b.date);
    state.messages.set(key, list);
    if (key === state.activeKey) {
      const listEl = $('msgList');
      const prevScroll = listEl.scrollTop;
      const prevHeight = listEl.scrollHeight;
      renderMessages(key);
      listEl.scrollTop = prevScroll + (listEl.scrollHeight - prevHeight);
    }
    renderConversations();
  }

  /* ---------- history loading ---------- */

  async function fetchJSON(path, params) {
    const res = await fetch(path + '?' + new URLSearchParams(params), { method: 'GET', credentials: 'include' });
    const text = await res.text();
    let data;
    try { data = JSON.parse(text); } catch (e) { throw new Error('服务器响应格式错误'); }
    if (data.status !== 'success') throw new Error(data.error || '请求失败');
    return data;
  }

  function normalizeMessages(rawList, defaultFrom) {
    return (rawList || []).map((m) => ({
      date: m.date || 0,
      from: (m.from !== undefined && m.from !== null) ? m.from : defaultFrom,
      type: m.type || 'TEXT',
      content: m.content || '',
    }));
  }

  async function loadHistory(reset) {
    const conv = activeConv();
    if (!conv || state.loading) return;
    if (!reset && !state.hasMore) return;
    state.loading = true;
    const key = keyOf(conv.kind, conv.id);
    const off = (state.offsets.get(key) || 0);
    const listEl = $('msgList');
    const loader = document.createElement('div');
    loader.className = 'msg-loading';
    loader.textContent = '正在加载历史…';
    if (reset) {
      state.messages.set(key, []);
      renderMessages(key);
    }
    listEl.prepend(loader);
    try {
      await waitForMyId();
      if (key !== state.activeKey) return;
      let got = 0;
      if (conv.kind === 'user') {
        const [fromPartner, fromMe] = await Promise.all([
          fetchJSON(REST.user, { senderId: conv.id, offset: off }),
          fetchJSON(REST.user, { senderId: state.myId, offset: off }),
        ]);
        const merged = normalizeMessages(fromPartner.messages, conv.id)
          .concat(normalizeMessages(fromMe.messages, state.myId));
        merged.sort((a, b) => a.date - b.date);
        prependHistory(key, merged);
        got = Math.max((fromPartner.messages || []).length, (fromMe.messages || []).length);
      } else {
        const data = await fetchJSON(REST.group, { groupId: conv.id, offset: off });
        prependHistory(key, normalizeMessages(data.messages, 0));
        got = (data.messages || []).length;
      }
      state.hasMore = got >= PAGE_SIZE;
      state.offsets.set(key, off + PAGE_SIZE);
    } catch (err) {
      showToast(err.message || '加载历史失败');
    } finally {
      if (loader.parentNode) loader.remove();
      state.loading = false;
      if (reset) scrollToBottom();
    }
  }

  /* ---------- sending ---------- */

  function updateInputState() {
    const conv = activeConv();
    const ok = state.connected && state.myId !== null && conv && conv.kind === 'user';
    $('inputBox').disabled = !ok;
    $('sendBtn').disabled = !ok;
    const box = $('inputBox');
    if (!ok) {
      box.placeholder = !conv ? '选择或新建一个会话' : (conv.kind === 'group' ? '群聊暂不支持发送，可查看历史记录' : '连接未就绪，正在重连…');
    } else {
      box.placeholder = '输入消息，Enter 发送，Shift + Enter 换行';
    }
  }

  function sendText() {
    const conv = activeConv();
    const box = $('inputBox');
    const text = box.value.trim();
    if (!conv || conv.kind !== 'user' || !text || !state.connected || state.myId === null) return;
    const inner = { type: 'TEXT', content: text };
    const payload = {
      code: 200,
      type: 'private_message',
      reason: 'ok',
      from: state.myId,
      to: conv.id,
      content: JSON.stringify(inner),
    };
    try { state.ws.send(JSON.stringify(payload)); }
    catch (e) { showToast('发送失败：连接异常'); return; }
    appendMessage(keyOf('user', conv.id), {
      date: Math.floor(Date.now() / 1000),
      from: state.myId,
      type: 'TEXT',
      content: text,
    });
    renderConversations();
    scrollToBottom();
    box.value = '';
    autosize();
  }

  /* ---------- conversation switching ---------- */

  function activeConv() {
    if (state.activeKey === null) return null;
    const [kind, id] = state.activeKey.split(':');
    return { kind, id: Number(id) };
  }

  function openConversation(conv) {
    state.activeKey = keyOf(conv.kind, conv.id);
    conv._unread = 0;
    state.hasMore = true;
    renderConversations();
    renderMessages(state.activeKey);
    refreshPeerTitle();
    updateInputState();
    loadHistory(true);
  }

  function scrollToBottom() {
    const listEl = $('msgList');
    listEl.scrollTop = listEl.scrollHeight;
  }

  function autosize() {
    const box = $('inputBox');
    box.style.height = 'auto';
    box.style.height = Math.min(box.scrollHeight, 140) + 'px';
  }

  /* ---------- modal ---------- */

  function openModal() {
    $('newConvModal').classList.remove('hidden');
    $('peerIdInput').value = '';
    $('peerIdInput').focus();
    updateModalTab();
  }

  function closeModal() {
    $('newConvModal').classList.add('hidden');
  }

  function updateModalTab() {
    const userMode = state.modalKind === 'user';
    $('tabUser').classList.toggle('active', userMode);
    $('tabGroup').classList.toggle('active', !userMode);
    $('idFieldLabel').textContent = userMode ? '对方用户 ID' : '目标群组 ID';
    $('idFieldHint').textContent = userMode
      ? '私聊消息通过 WebSocket 实时收发，可加载历史记录。'
      : '群聊仅支持查看历史消息，暂不支持发送。';
    $('peerIdInput').placeholder = userMode ? '请输入对方用户 ID' : '请输入目标群组 ID';
  }

  function confirmNewConv() {
    const id = Number($('peerIdInput').value.trim());
    if (!Number.isInteger(id) || id <= 0) { showToast('请输入有效的 ID'); return; }
    ensureConversation(state.modalKind, id);
    closeModal();
    const conv = { kind: state.modalKind, id };
    openConversation(conv);
  }

  /* ---------- init ---------- */

  function init() {
    connectWS();

    $('logoutBtn').addEventListener('click', () => { window.location.href = '/pages/login.html'; });
    $('newConvBtn').addEventListener('click', openModal);
    $('cancelConvBtn').addEventListener('click', closeModal);
    $('confirmConvBtn').addEventListener('click', confirmNewConv);
    $('peerIdInput').addEventListener('keydown', (e) => { if (e.key === 'Enter') confirmNewConv(); });
    $('tabUser').addEventListener('click', () => { state.modalKind = 'user'; updateModalTab(); });
    $('tabGroup').addEventListener('click', () => { state.modalKind = 'group'; updateModalTab(); });
    $('newConvModal').addEventListener('click', (e) => { if (e.target === e.currentTarget) closeModal(); });

    const box = $('inputBox');
    box.addEventListener('input', autosize);
    box.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendText(); }
    });
    $('sendBtn').addEventListener('click', sendText);

    const listEl = $('msgList');
    listEl.addEventListener('scroll', () => {
      if (listEl.scrollTop < 40) loadHistory(false);
    });

    renderConversations();
    refreshPeerTitle();
    updateInputState();
    if (state.conversations.length) openConversation(state.conversations[0]);
  }

  document.addEventListener('DOMContentLoaded', init);
})();
