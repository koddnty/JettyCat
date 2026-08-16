import React, { useEffect, useLayoutEffect, useMemo, useRef, useState, useCallback } from 'react';
import Chat, { Bubble, useMessages } from '@chatui/core';
import { useChat } from '../ws';

const HISTORY_PAGE = 10;

function keyOf(kind, id) {
  return `${kind}:${id}`;
}

export default function ChatSection({ active }) {
  const { myId, connected, subscribe, sendMessage, showNotice, setActiveKey, recordLastMessage } = useChat();
  const { messages, appendMsg, prependMsgs, resetList } = useMessages([]);
  const [typing, setTyping] = useState(false);
  const typingTimerRef = useRef(null);
  const messagesMapRef = useRef(new Map());
  const requestIdRef = useRef(0);
  const messageIdRef = useRef(0);
  const streamsRef = useRef(null);
  const loadingRef = useRef(false);
  const scrollerRef = useRef(null);
  const scrollAnchorRef = useRef(null);
  const activeRef = useRef(active);
  activeRef.current = active;

  // 用基本类型(而非 active 对象引用)作为副作用依赖, 避免父组件每次重渲染
  // 都导致历史记录重新拉取。fetch_* 接口只在切换会话(activeKey 变化)时调用。
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

  // Fill a stream's queue from its next page when empty; pages are appended
  // newest-first so each queue stays sorted newest → oldest.
  const fillStream = useCallback(
    async (stream) => {
      const s = streamsRef.current;
      if (!s || s[stream].queue.length > 0 || s[stream].done) return;
      const items = await fetchPage(stream);
      if (items.length) s[stream].queue.push(...items);
    },
    [fetchPage]
  );

  // Merge the two queues' newest-available items into a newest-first batch.
  // When a queue is drained it is refilled immediately so the two streams
  // stay interleaved chronologically without disorder. 群聊只有单一消息流
  // (theirs), mine 流为空所以直接退化为顺序拉取。
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
        // 历史记录与已存在(乐观发送/实时)的消息去重, 避免同一消息显示两次
        const seen = new Set(pendingLive.map((m) => `${m.date}|${m.from}|${m.content}`));
        const fresh = entries.filter((m) => !seen.has(`${m.date}|${m.from}|${m.content}`));
        const list = [...fresh, ...pendingLive].sort(compareMessages);
        messagesMapRef.current.set(key, list);
        resetList(toChatUiMessages(list, myId));
      } catch (error) {
        if (requestId !== requestIdRef.current) return;
        showNotice('历史消息', '历史消息暂时无法加载');
      } finally {
        if (requestId === requestIdRef.current) loadingRef.current = false;
      }
    },
    [myId, nextMessageId, resetList, showNotice, resetStreams, nextMergedBatch]
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

      // Anchor the scroll position so prepended older messages do not shift
      // the currently visible content (which would otherwise leave scrollTop
      // pinned at 0 and keep re-triggering the "load older" handler).
      const scroller = scrollerRef.current;
      if (scroller && typeof scroller.scrollHeight === 'number' && typeof scroller.scrollTop === 'number') {
        scrollAnchorRef.current = {
          height: scroller.scrollHeight,
          top: scroller.scrollTop,
        };
      }
      prependMsgs(toChatUiMessages(fresh, myId));
    } catch (error) {
      showNotice('历史消息', '历史消息暂时无法加载');
    } finally {
      loadingRef.current = false;
    }
  }, [myId, nextMessageId, prependMsgs, showNotice, nextMergedBatch]);

  // After new (older) messages are committed to the DOM, restore the scroll
  // offset so the previously visible messages stay in place.
  useLayoutEffect(() => {
    const anchor = scrollAnchorRef.current;
    const scroller = scrollerRef.current;
    if (!anchor || !scroller) return;
    scrollAnchorRef.current = null;
    const added = scroller.scrollHeight - anchor.height;
    const target = anchor.top + added;
    if (target > 0) scroller.scrollTop = target;
  });

  // 仅当会话切换(activeKey 变化)时拉取一次历史消息; 发送/接收消息不再触发 fetch
  useEffect(() => {
    const conv = activeRef.current;
    setActiveKey(activeKey);
    if (conv) {
      loadHistory(conv);
    } else {
      ++requestIdRef.current;
      messagesMapRef.current.clear();
      resetList([]);
    }
  }, [activeKey, loadHistory, resetList, setActiveKey]);

  useEffect(() => {
    return subscribe(({ type, inner, sender, receiver, groupId }) => {
      const conv = activeRef.current;
      if (!conv) return;

      if (type === 'private_message') {
        if (conv.kind !== 'user') return;
        if (Number(sender) !== Number(conv.id) && Number(receiver) !== Number(conv.id)) return;
        if (Number(sender) !== Number(conv.id)) return;
      } else if (type === 'group_message') {
        if (conv.kind !== 'group') return;
        if (Number(groupId) !== Number(conv.id)) return;
        // 自己其他端的回显（后端跳过本端 session，但同 userId 的其他端会收到），跳过避免重复
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
      appendMsg(toChatUiMessage(entry, myId, prevDate));
    });
  }, [subscribe, myId, appendMsg, nextMessageId]);

  useEffect(() => {
    return () => clearTimeout(typingTimerRef.current);
  }, []);

  const handleSend = useCallback(
    (type, val) => {
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
        appendMsg(toChatUiMessage(entry, myId, prevDate));
        sendMessage(conv.id, content, conv.kind);
        recordLastMessage(conv.kind, conv.id, content, now);
        setTyping(true);
        clearTimeout(typingTimerRef.current);
        typingTimerRef.current = setTimeout(() => setTyping(false), 1400);
      }
    },
    [myId, appendMsg, nextMessageId, recordLastMessage, sendMessage]
  );

  const quickReplies = useMemo(
    () => [
      { name: '在吗', code: 'hi' },
      { name: '收到', code: 'ok' },
      { name: '你好', code: 'hello' },
    ],
    []
  );

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
        navbar={{ title, desc: connected ? '实时在线' : '连接中…', align: 'left' }}
        messages={messages}
        renderMessageContent={renderMessageContent}
        onSend={handleSend}
        onScroll={handleScroll}
        placeholder="输入消息，Enter 发送，Shift + Enter 换行"
        quickReplies={quickReplies}
        onQuickReplyClick={(item) => handleSend('text', item.name)}
        isTyping={typing}
      />
    </div>
  );
}

function toChatUiMessages(list, myId) {
  let prevDate = 0;
  return list.map((item) => {
    const msg = toChatUiMessage(item, myId, prevDate);
    prevDate = Number(item.date);
    return msg;
  });
}

function compareMessages(a, b) {
  return a.date - b.date || sequenceOf(a.id) - sequenceOf(b.id);
}

function sequenceOf(id) {
  const match = /-(\d+)$/.exec(String(id));
  return match ? Number(match[1]) : 0;
}

const TIME_GAP = 5 * 60 * 1000;

function toChatUiMessage(item, myId, prevDate = 0) {
  const createdAt = Number(item.date) * 1000;
  const prevAt = Number(prevDate) * 1000;
  const showTime = prevAt === 0 || createdAt - prevAt >= TIME_GAP;
  return {
    _id: item.id,
    type: 'text',
    content: { text: item.content || '' },
    position: Number(item.from) === Number(myId) ? 'right' : 'left',
    createdAt,
    hasTime: showTime,
    user: {
      name: Number(item.from) === Number(myId) ? '我' : '他',
    },
  };
}
