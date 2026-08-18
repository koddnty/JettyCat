import React, { useEffect, useLayoutEffect, useMemo, useRef, useState, useCallback } from 'react';
import Chat, { Bubble, useMessages } from '@chatui/core';
import { useChat } from '../ws';

const toolbarIcon = (body) => `data:image/svg+xml,${encodeURIComponent(
  `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#42454b" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${body}</svg>`
)}`;

const TOOLBAR_ICONS = {
  emoji: toolbarIcon('<circle cx="12" cy="12" r="9"/><circle cx="9" cy="10" r=".7" fill="#42454b"/><circle cx="15" cy="10" r=".7" fill="#42454b"/><path d="M8.5 14.2c1.8 2 5.2 2 7 0"/>'),
  sticker: toolbarIcon('<path d="m12 3 8 4.5v9L12 21l-8-4.5v-9L12 3Z"/><path d="m4.5 7.5 7.5 4 7.5-4M12 11.5V21"/>'),
  file: toolbarIcon('<path d="M3.5 6.5h6l1.6 2H20.5v9.8a1.7 1.7 0 0 1-1.7 1.7H5.2a1.7 1.7 0 0 1-1.7-1.7V6.5Z"/><path d="M3.5 9h17"/>'),
  screenshot: toolbarIcon('<circle cx="7" cy="7" r="2.2"/><path d="M3 11V6a3 3 0 0 1 3-3h5M13 3h5a3 3 0 0 1 3 3v5M21 13v5a3 3 0 0 1-3 3h-5M11 21H6a3 3 0 0 1-3-3v-5M4 20 20 4"/>'),
  more: toolbarIcon('<path d="m8 9 4 4 4-4"/>'),
  chat: toolbarIcon('<path d="M20 11a7.5 7.5 0 0 1-11 6.6L4 19l1.5-4A7.5 7.5 0 1 1 20 11Z"/><path d="M8.5 11h.01M12 11h.01M15.5 11h.01"/>'),
};

const HISTORY_PAGE = 10;

function keyOf(kind, id) {
  return `${kind}:${id}`;
}

export default function ChatSection({ active }) {
  const { myId, myName, myAvatarUrl, connected, subscribe, sendMessage, showNotice, setActiveKey, recordLastMessage, fetchUserProfile } = useChat();
  const { messages, appendMsg, prependMsgs, resetList } = useMessages([]);
  const [typing, setTyping] = useState(false);
  const [avatarProfile, setAvatarProfile] = useState(null);
  const typingTimerRef = useRef(null);
  const messagesMapRef = useRef(new Map());
  const profileCacheRef = useRef(new Map());
  const inflightRef = useRef(new Map());
  const requestIdRef = useRef(0);
  const messageIdRef = useRef(0);
  const streamsRef = useRef(null);
  const loadingRef = useRef(false);
  const scrollerRef = useRef(null);
  const scrollAnchorRef = useRef(null);
  const activeRef = useRef(active);
  activeRef.current = active;

  const activeKey = active ? keyOf(active.kind, active.id) : null;

  const nextMessageId = useCallback((prefix) => {
    messageIdRef.current += 1;
    return `${prefix}-${messageIdRef.current}`;
  }, []);

  const resetStreams = useCallback((kind) => {
    const isGroup = kind === 'group';
    streamsRef.current = {
      theirs: { offset: 0, done: false, queue: [] },
      mine: isGroup ? { offset: 0, done: true, queue: [] } : { offset: 0, done: false, queue: [] },
    };
    loadingRef.current = false;
  }, []);

  const fetchPage = useCallback(
    async (stream) => {
      const conv = activeRef.current;
      if (!conv || (conv.kind !== 'user' && conv.kind !== 'group') || myId === null) return [];
      const isTheirs = stream === 'theirs';
      const params = new URLSearchParams();
      if (conv.kind === 'group') {
        params.set('groupId', String(conv.id));
      } else if (isTheirs) {
        params.set('senderId', String(conv.id));
      } else {
        params.set('senderId', String(myId));
        params.set('receiverId', String(conv.id));
      }
      params.set('offset', String(streamsRef.current[stream].offset));
      const endpoint = conv.kind === 'group' ? '/api/chat/fetch_group_message' : '/api/chat/fetch_user_message';
      const response = await fetch(`${endpoint}?${params}`, { credentials: 'include' });
      const page = await response.json();
      if (page.code !== 200) throw new Error(page.msg || 'history request failed');
      const items = Array.isArray(page.data && page.data.messages) ? page.data.messages : [];
      streamsRef.current[stream].offset += items.length;
      if (items.length < HISTORY_PAGE) streamsRef.current[stream].done = true;
      return items;
    },
    [myId]
  );

  const fillStream = useCallback(
    async (stream) => {
      const s = streamsRef.current;
      if (!s || s[stream].queue.length > 0 || s[stream].done) return;
      const items = await fetchPage(stream);
      if (items.length) s[stream].queue.push(...items);
    },
    [fetchPage]
  );

  // 根据发送者 id 解析聊天里要显示的昵称与头像(用于 QQ 式的消息气泡)。
  const resolveDisplayMeta = useCallback(
    (fromId) => {
      const from = Number(fromId);
      const conv = activeRef.current;
      const key = `user:${from}`;

      if (profileCacheRef.current.has(key)) return Promise.resolve(profileCacheRef.current.get(key));
      if (inflightRef.current.has(key)) return inflightRef.current.get(key);

      if (from === Number(myId)) {
        const meta = { name: myName || '我', avatar: myAvatarUrl || '' };
        profileCacheRef.current.set(key, meta);
        return Promise.resolve(meta);
      }

      if (conv && conv.kind === 'user' && from === Number(conv.id)) {
        const meta = { name: conv.name || `用户 ${from}`, avatar: conv.avatarUrl || '' };
        profileCacheRef.current.set(key, meta);
        return Promise.resolve(meta);
      }

      const promise = (async () => {
        const prof = await fetchUserProfile(from);
        const meta = prof
          ? { name: prof.nickname || prof.username || `用户 ${from}`, avatar: prof.avatarUrl || '' }
          : { name: `用户 ${from}`, avatar: '' };
        profileCacheRef.current.set(key, meta);
        inflightRef.current.delete(key);
        return meta;
      })();
      inflightRef.current.set(key, promise);
      return promise;
    },
    [myId, myName, myAvatarUrl, fetchUserProfile]
  );

  // 点击头像时：优先用缓存，无缓存则拉取
  const handleAvatarClick = useCallback(
    async (fromId) => {
      const from = Number(fromId);
      if (!Number.isInteger(from) || from < 1) return;
      const key = `user:${from}`;
      // 优先用缓存
      if (profileCacheRef.current.has(key)) {
        const cached = profileCacheRef.current.get(key);
        setAvatarProfile({ id: from, ...cached });
        return;
      }
      // 无缓存：拉取
      const prof = await fetchUserProfile(from);
      if (prof) {
        const meta = { name: prof.nickname || prof.username || `用户 ${from}`, avatar: prof.avatarUrl || '' };
        profileCacheRef.current.set(key, meta);
        setAvatarProfile({ id: from, ...meta });
      } else {
        setAvatarProfile({ id: from, name: `用户 ${from}`, avatar: '' });
      }
    },
    [fetchUserProfile]
  );

  const nextMergedBatch = useCallback(
    async (count) => {
      const result = [];
      while (result.length < count) {
        const s = streamsRef.current;
        if (!s) break;
        await Promise.all([fillStream('theirs'), fillStream('mine')]);
        const a = s.theirs.queue[0];
        const b = s.mine.queue[0];
        if (!a && !b) break;
        if (!a) {
          result.push(s.mine.queue.shift());
        } else if (!b) {
          result.push(s.theirs.queue.shift());
        } else if (Number(a.date) >= Number(b.date)) {
          result.push(s.theirs.queue.shift());
        } else {
          result.push(s.mine.queue.shift());
        }
      }
      return result;
    },
    [fillStream]
  );

  const loadHistory = useCallback(
    async (conversation) => {
      if (!conversation || (conversation.kind !== 'user' && conversation.kind !== 'group') || myId === null) return;
      const key = keyOf(conversation.kind, conversation.id);
      const requestId = ++requestIdRef.current;
      const pendingLive = (messagesMapRef.current.get(key) || []).filter((m) => m.origin !== 'history');
      resetList([]);
      resetStreams(conversation.kind);
      loadingRef.current = true;
      try {
        const batch = await nextMergedBatch(10);
        if (requestId !== requestIdRef.current) return;
        const entries = batch
          .map((item) => ({
            id: nextMessageId('history'),
            origin: 'history',
            date: Number(item.date) || 0,
            from: Number(item.from),
            content: item.content == null ? '' : String(item.content),
          }))
          .sort(compareMessages);
        const seen = new Set(pendingLive.map((m) => `${m.date}|${m.from}|${m.content}`));
        const fresh = entries.filter((m) => !seen.has(`${m.date}|${m.from}|${m.content}`));
        const list = [...fresh, ...pendingLive].sort(compareMessages);
        messagesMapRef.current.set(key, list);
        resetList(await toChatUiMessages(list, myId, resolveDisplayMeta));
      } catch (error) {
        if (requestId !== requestIdRef.current) return;
        showNotice('历史消息', '历史消息暂时无法加载');
      } finally {
        if (requestId === requestIdRef.current) loadingRef.current = false;
      }
    },
    [myId, nextMessageId, resetList, showNotice, resetStreams, nextMergedBatch, resolveDisplayMeta]
  );

  const loadOlder = useCallback(async () => {
    if (loadingRef.current) return;
    const conv = activeRef.current;
    if (!conv || (conv.kind !== 'user' && conv.kind !== 'group') || myId === null) return;
    const key = keyOf(conv.kind, conv.id);
    loadingRef.current = true;
    try {
      const batch = await nextMergedBatch(10);
      const entries = batch
        .map((item) => ({
          id: nextMessageId('history'),
          origin: 'history',
          date: Number(item.date) || 0,
          from: Number(item.from),
          content: item.content == null ? '' : String(item.content),
        }))
        .sort(compareMessages);
      const existing = messagesMapRef.current.get(key) || [];
      const seen = new Set(existing.map((m) => `${m.date}|${m.from}|${m.content}`));
      const fresh = entries.filter((m) => !seen.has(`${m.date}|${m.from}|${m.content}`));
      if (fresh.length === 0) return;
      const list = [...fresh, ...existing].sort(compareMessages);
      messagesMapRef.current.set(key, list);

      const scroller = scrollerRef.current;
      if (scroller && typeof scroller.scrollHeight === 'number' && typeof scroller.scrollTop === 'number') {
        scrollAnchorRef.current = {
          height: scroller.scrollHeight,
          top: scroller.scrollTop,
        };
      }
      prependMsgs(await toChatUiMessages(fresh, myId, resolveDisplayMeta));
    } catch (error) {
      showNotice('历史消息', '历史消息暂时无法加载');
    } finally {
      loadingRef.current = false;
    }
  }, [myId, nextMessageId, prependMsgs, showNotice, nextMergedBatch, resolveDisplayMeta]);

  useLayoutEffect(() => {
    const anchor = scrollAnchorRef.current;
    const scroller = scrollerRef.current;
    if (!anchor || !scroller) return;
    scrollAnchorRef.current = null;
    const added = scroller.scrollHeight - anchor.height;
    const target = anchor.top + added;
    if (target > 0) scroller.scrollTop = target;
  });

  useEffect(() => {
    const conv = activeRef.current;
    setActiveKey(activeKey);
    if (conv) {
      loadHistory(conv);
    } else {
      ++requestIdRef.current;
      messagesMapRef.current.clear();
      inflightRef.current.clear();
      resetList([]);
    }
  }, [activeKey, loadHistory, resetList, setActiveKey]);

  useEffect(() => {
    return subscribe(async ({ type, inner, sender, receiver, groupId }) => {
      const conv = activeRef.current;
      if (!conv) return;

      if (type === 'private_message') {
        if (conv.kind !== 'user') return;
        if (Number(sender) !== Number(conv.id) && Number(receiver) !== Number(conv.id)) return;
        if (Number(sender) !== Number(conv.id)) return;
      } else if (type === 'group_message') {
        if (conv.kind !== 'group') return;
        if (Number(groupId) !== Number(conv.id)) return;
        if (Number(sender) === Number(myId)) return;
      } else {
        return;
      }

      const key = keyOf(conv.kind, conv.id);
      const list = messagesMapRef.current.get(key) || [];
      const entry = {
        id: nextMessageId('live'),
        origin: 'live',
        date: (inner && inner.date) || Math.floor(Date.now() / 1000),
        from: Number((inner && inner.from !== undefined) ? inner.from : sender),
        content: (inner && inner.content) || '',
      };
      list.push(entry);
      list.sort(compareMessages);
      messagesMapRef.current.set(key, list);
      const prevDate = list.length > 1 ? list[list.length - 2].date : 0;
      appendMsg(await toChatUiMessage(entry, myId, prevDate, resolveDisplayMeta));
    });
  }, [subscribe, myId, appendMsg, nextMessageId, resolveDisplayMeta]);

  useEffect(() => {
    return () => clearTimeout(typingTimerRef.current);
  }, []);

  const handleSend = useCallback(
    async (type, val) => {
      const conv = activeRef.current;
      if (type === 'text' && val.trim() && conv) {
        const content = val.trim();
        const now = Math.floor(Date.now() / 1000);
        const entry = { id: nextMessageId('local'), origin: 'local', date: now, from: myId, content };
        const key = keyOf(conv.kind, conv.id);
        const list = messagesMapRef.current.get(key) || [];
        list.push(entry);
        list.sort(compareMessages);
        messagesMapRef.current.set(key, list);
        const prevDate = list.length > 1 ? list[list.length - 2].date : 0;
        appendMsg(await toChatUiMessage(entry, myId, prevDate, resolveDisplayMeta));
        sendMessage(conv.id, content, conv.kind);
        recordLastMessage(conv.kind, conv.id, content, now);
        setTyping(true);
        clearTimeout(typingTimerRef.current);
        typingTimerRef.current = setTimeout(() => setTyping(false), 1400);
      }
    },
    [myId, appendMsg, nextMessageId, recordLastMessage, sendMessage, resolveDisplayMeta]
  );

  // 图片/文件上传（预留接口，未来实现后端对接）
  const handleImageSend = useCallback(async (file) => {
    showNotice('图片上传', '图片上传功能即将上线');
    return null;
  }, [showNotice]);

  const handleToolbarClick = useCallback((item) => {
    if (item.type === 'image') {
      // 触发隐藏的文件选择器
      const input = document.createElement('input');
      input.type = 'file';
      input.accept = 'image/*';
      input.onchange = (e) => {
        const file = e.target.files[0];
        if (file) handleImageSend(file);
      };
      input.click();
    } else if (item.type === 'file') {
      showNotice('文件上传', '文件上传功能即将上线');
    } else if (item.type === 'emoji') {
      showNotice('表情', '表情功能即将上线');
    } else if (item.type === 'sticker') {
      showNotice('扩展功能', '扩展功能即将上线');
    } else if (item.type === 'more') {
      showNotice('更多功能', '更多功能即将上线');
    } else if (item.type === 'chat') {
      showNotice('聊天工具', '聊天工具即将上线');
    }
  }, [handleImageSend, showNotice]);

  // 工具栏图标先保留界面入口，尚未完成的能力点击后提示用户。
  const toolbar = useMemo(() => [
    { type: 'emoji', img: TOOLBAR_ICONS.emoji, title: '表情' },
    { type: 'sticker', img: TOOLBAR_ICONS.sticker, title: '扩展' },
    { type: 'file', img: TOOLBAR_ICONS.file, title: '文件' },
    { type: 'image', img: TOOLBAR_ICONS.screenshot, title: '截图' },
    { type: 'more', img: TOOLBAR_ICONS.more, title: '更多' },
    { type: 'chat', img: TOOLBAR_ICONS.chat, title: '聊天工具' },
  ], []);

  // 自定义消息渲染：支持头像点击
  const renderMessageContent = useCallback(
    (msg) => {
      if (msg.type === 'text') {
        return <Bubble content={msg.content.text} />;
      }
      return null;
    },
    []
  );

  const handleScroll = useCallback(
    (event) => {
      const el = event && event.target;
      if (!el || typeof el.scrollTop !== 'number') return;
      scrollerRef.current = el;
      if (el.scrollTop < 40 && !loadingRef.current) loadOlder();
    },
    [loadOlder]
  );

  if (!active || active.add) {
    return (
      <div className="chat-module">
        <div className="chat-empty">
          <div className="chat-empty-inner">
            <span className="chat-empty-icon">
              <svg focusable="false" aria-hidden="true"><use href="#icon-chat" /></svg>
            </span>
            <h2>从左侧选择一个会话</h2>
            <p>实时消息和历史记录都会集中在这里。</p>
          </div>
        </div>
      </div>
    );
  }

  const title = active.name || (active.kind === 'group' ? `群组 ${active.id}` : `用户 ${active.id}`);

  return (
    <div className="chat-module">
      <Chat
        wideBreakpoint="0px"
        navbar={{ title, desc: connected ? '实时在线' : '连接中…', align: 'left' }}
        messages={messages}
        renderMessageContent={renderMessageContent}
        onSend={handleSend}
        onScroll={handleScroll}
        placeholder="输入消息..."
        isTyping={typing}
        toolbar={toolbar}
        onToolbarClick={handleToolbarClick}
        onImageSend={handleImageSend}
      />

      {/* 头像点击后显示的用户资料卡片 */}
      {avatarProfile && (
        <div className="avatar-profile-dialog" role="dialog" aria-modal="true">
          <div className="avatar-profile-backdrop" onClick={() => setAvatarProfile(null)} />
          <div className="avatar-profile-card">
            {avatarProfile.avatar ? (
              <img className="avatar-profile-img" src={avatarProfile.avatar} alt={avatarProfile.name} />
            ) : (
              <span className="avatar-profile-img avatar-profile-fallback">{initials(avatarProfile.name)}</span>
            )}
            <div className="avatar-profile-name">{avatarProfile.name}</div>
            <div className="avatar-profile-id">ID {avatarProfile.id}</div>
            <div className="avatar-profile-actions">
              <button type="button" className="avatar-profile-btn" onClick={() => setAvatarProfile(null)}>关闭</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

function initials(name) {
  return String(name || '?').trim().slice(0, 2).toUpperCase();
}

function toChatUiMessages(list, myId, resolveDisplayMeta) {
  let prevDate = 0;
  return Promise.all(
    list.map(async (item) => {
      const msg = await toChatUiMessage(item, myId, prevDate, resolveDisplayMeta);
      prevDate = Number(item.date);
      return msg;
    })
  );
}

function compareMessages(a, b) {
  return a.date - b.date || sequenceOf(a.id) - sequenceOf(b.id);
}

function sequenceOf(id) {
  const match = /-(\d+)$/.exec(String(id));
  return match ? Number(match[1]) : 0;
}

const TIME_GAP = 5 * 60 * 1000;

async function toChatUiMessage(item, myId, prevDate = 0, resolveDisplayMeta = null) {
  const createdAt = Number(item.date) * 1000;
  const prevAt = Number(prevDate) * 1000;
  const showTime = prevAt === 0 || createdAt - prevAt >= TIME_GAP;
  const isSelf = Number(item.from) === Number(myId);
  let meta = { name: isSelf ? '我' : '他', avatar: '' };
  if (resolveDisplayMeta) {
    const resolved = await resolveDisplayMeta(item.from);
    if (resolved) meta = resolved;
  }
  return {
    _id: item.id,
    type: 'text',
    content: { text: item.content || '' },
    position: isSelf ? 'right' : 'left',
    createdAt,
    hasTime: showTime,
    user: {
      name: meta.name,
      avatar: meta.avatar,
      avatarAlt: meta.name,
    },
    _from: Number(item.from),
  };
}
