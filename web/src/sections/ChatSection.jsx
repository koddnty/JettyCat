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

  const nextMessageId = useCallback((prefix) => {
    messageIdRef.current += 1;
    return `${prefix}-${messageIdRef.current}`;
  }, []);

  const resetStreams = useCallback(() => {
    streamsRef.current = {
      theirs: { offset: 0, done: false, queue: [] },
      mine: { offset: 0, done: false, queue: [] },
    };
    loadingRef.current = false;
  }, []);

  const fetchPage = useCallback(
    async (stream) => {
      const isTheirs = stream === 'theirs';
      const params = new URLSearchParams();
      if (isTheirs) {
        params.set('senderId', String(active.id));
      } else {
        params.set('senderId', String(myId));
        params.set('receiverId', String(active.id));
      }
      params.set('offset', String(streamsRef.current[stream].offset));
      const response = await fetch(`/api/chat/fetch_user_message?${params}`, { credentials: 'include' });
      const page = await response.json();
      if (page.status !== 'success') throw new Error(page.error || 'history request failed');
      const items = Array.isArray(page.messages) ? page.messages : [];
      streamsRef.current[stream].offset += items.length;
      if (items.length < HISTORY_PAGE) streamsRef.current[stream].done = true;
      return items;
    },
    [active, myId]
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
  // stay interleaved chronologically without disorder.
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
      if (!conversation || conversation.kind !== 'user' || myId === null) return;
      const key = keyOf(conversation.kind, conversation.id);
      const requestId = ++requestIdRef.current;
      const pendingLive = (messagesMapRef.current.get(key) || []).filter((m) => m.origin !== 'history');
      resetList([]);
      resetStreams();
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
        const list = [...entries, ...pendingLive].sort(compareMessages);
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
    if (!active || active.kind !== 'user' || myId === null) return;
    const key = keyOf(active.kind, active.id);
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
  }, [active, myId, nextMessageId, prependMsgs, showNotice, nextMergedBatch]);

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

  useEffect(() => {
    setActiveKey(active ? keyOf(active.kind, active.id) : null);
    if (active) {
      loadHistory(active);
    } else {
      ++requestIdRef.current;
      messagesMapRef.current.clear();
      resetList([]);
    }
  }, [active, loadHistory, resetList, setActiveKey]);

  useEffect(() => {
    return subscribe(({ type, inner, sender, receiver }) => {
      if (type !== 'private_message' || !active) return;
      if (Number(sender) !== Number(active.id) && Number(receiver) !== Number(active.id)) return;
      if (Number(sender) !== Number(active.id)) return;
      const key = keyOf(active.kind, active.id);
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
  }, [subscribe, active, myId, appendMsg, nextMessageId]);

  useEffect(() => {
    return () => clearTimeout(typingTimerRef.current);
  }, []);

  const handleSend = useCallback(
    (type, val) => {
      if (type === 'text' && val.trim() && active) {
        const content = val.trim();
        const now = Math.floor(Date.now() / 1000);
        const entry = { id: nextMessageId('local'), origin: 'local', date: now, from: myId, content };
        const key = keyOf(active.kind, active.id);
        const list = messagesMapRef.current.get(key) || [];
        list.push(entry);
        list.sort(compareMessages);
        messagesMapRef.current.set(key, list);
        const prevDate = list.length > 1 ? list[list.length - 2].date : 0;
        appendMsg(toChatUiMessage(entry, myId, prevDate));
        sendMessage(active.id, content);
        recordLastMessage('user', active.id, content);
        setTyping(true);
        clearTimeout(typingTimerRef.current);
        typingTimerRef.current = setTimeout(() => setTyping(false), 1400);
      }
    },
    [active, myId, appendMsg, nextMessageId, recordLastMessage, sendMessage]
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

  const title = active.kind === 'group' ? `群组 ${active.id}` : `用户 ${active.id}`;

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
