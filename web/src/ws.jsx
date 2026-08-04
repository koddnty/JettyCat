import React, { createContext, useContext, useCallback, useEffect, useRef, useState } from 'react';

const ChatContext = createContext(null);
export const useChat = () => useContext(ChatContext);

const WS_PATH = '/api/chat';
const CONV_STORE = 'jettycat.chat.conversations';

function loadConversations() {
  try {
    const value = JSON.parse(localStorage.getItem(CONV_STORE) || '[]');
    return Array.isArray(value)
      ? value.filter((item) => item && (item.kind === 'user' || item.kind === 'group') && Number(item.id) > 0)
      : [];
  } catch (error) {
    return [];
  }
}

function saveConversations(list) {
  try {
    localStorage.setItem(CONV_STORE, JSON.stringify(list));
  } catch (error) {
    /* storage is optional */
  }
}

function parseContent(content) {
  try {
    return typeof content === 'string' ? JSON.parse(content) : content;
  } catch (error) {
    return null;
  }
}

export function ChatProvider({ children }) {
  const wsRef = useRef(null);
  const reconnectRef = useRef(0);
  const authFailedRef = useRef(false);
  const listenersRef = useRef(new Set());
  const activeKeyRef = useRef(null);
  const myIdRef = useRef(null);
  const sendQueueRef = useRef([]);
  const flushingRef = useRef(false);
  const aliveRef = useRef(true);
  const noticeTimersRef = useRef(new Map());
  const [connected, setConnected] = useState(false);
  const [myId, setMyId] = useState(null);
  const [myName, setMyName] = useState('jettyCat 用户');
  const [conversations, setConversations] = useState(loadConversations);
  const [notices, setNotices] = useState([]);
  const [unread, setUnread] = useState({});
  const [lastMessages, setLastMessages] = useState({});

  const notify = useCallback((payload) => {
    listenersRef.current.forEach((fn) => fn(payload));
  }, []);

  const showNotice = useCallback((title, content, sender) => {
    const id = Date.now() + Math.random();
    setNotices((prev) => [...prev.slice(-2), { id, title, content, sender }]);
    setTimeout(() => setNotices((prev) => prev.filter((n) => n.id !== id)), 5000);
  }, []);

  const dismissNotice = useCallback((id) => {
    setNotices((prev) => prev.filter((n) => n.id !== id));
  }, []);

  const pushMessageNotice = useCallback((sender, preview) => {
    setUnread((prev) => ({
      ...prev,
      [`user:${sender}`]: (prev[`user:${sender}`] || 0) + 1,
    }));
    setNotices((prev) => {
      const existing = prev.find((n) => n.kind === 'message' && n.sender === sender);
      if (existing) {
        const count = (existing.count || 1) + 1;
        return prev.map((n) =>
          n === existing
            ? { ...n, count, title: '收到新消息', content: `来自同一用户的 ${count} 条新消息` }
            : n
        );
      }
      return [
        ...prev,
        { id: Date.now() + Math.random(), kind: 'message', sender, count: 1, title: '收到新消息', content: preview || '你有一条新的私聊消息' },
      ];
    });

    const timerId = noticeTimersRef.current.get(sender);
    if (timerId) clearTimeout(timerId);
    const timer = setTimeout(() => {
      setNotices((prev) => prev.filter((n) => !(n.kind === 'message' && n.sender === sender)));
      noticeTimersRef.current.delete(sender);
    }, 5000);
    noticeTimersRef.current.set(sender, timer);
  }, []);

  const clearUnread = useCallback((key) => {
    setUnread((prev) => {
      if (!prev[key]) return prev;
      const next = { ...prev };
      delete next[key];
      return next;
    });
  }, []);

  const recordLastMessage = useCallback((kind, id, content) => {
    setLastMessages((prev) => ({ ...prev, [`${kind}:${id}`]: content }));
  }, []);

  const flushQueue = useCallback(() => {
    if (flushingRef.current) return;
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    if (myIdRef.current === null) return;
    const queue = sendQueueRef.current;
    if (queue.length === 0) return;

    flushingRef.current = true;

    const sendNext = () => {
      if (!aliveRef.current) {
        flushingRef.current = false;
        return;
      }
      const socket = wsRef.current;
      if (!socket || socket.readyState !== WebSocket.OPEN || myIdRef.current === null) {
        flushingRef.current = false;
        return;
      }
      const item = queue.shift();
      if (!item) {
        flushingRef.current = false;
        return;
      }
      const payload = JSON.stringify({
        code: 200,
        type: 'private_message',
        reason: 'ok',
        from: myIdRef.current,
        to: Number(item.to),
        content: JSON.stringify({ type: 'TEXT', from: myIdRef.current, date: 0, content: item.content }),
      });
      try {
        socket.send(payload);
      } catch (error) {
        // Put back on failure so the message isn't lost; retry on next flush.
        queue.unshift(item);
        flushingRef.current = false;
        return;
      }
      // Wait for the frame to fully drain before sending the next one,
      // guaranteeing strictly sequential delivery without interleaving.
      const waitDrain = (startedAt) => {
        if (!aliveRef.current) {
          flushingRef.current = false;
          return;
        }
        const current = wsRef.current;
        if (!current || current.readyState !== WebSocket.OPEN) {
          flushingRef.current = false;
          return;
        }
        if (current.bufferedAmount === 0 || Date.now() - startedAt > 500) {
          sendNext();
        } else {
          setTimeout(() => waitDrain(startedAt), 4);
        }
      };
      waitDrain(Date.now());
    };

    sendNext();
  }, []);

  const handleMessage = useCallback(
    (raw) => {
      let message;
      try {
        message = JSON.parse(raw);
      } catch (error) {
        return;
      }
      if (message.code === 401) {
        authFailedRef.current = true;
        showNotice('登录状态已失效', '请重新登录后继续使用');
        return;
      }
      if (message.from === 0 && message.to) {
        myIdRef.current = Number(message.to);
        setMyId(Number(message.to));
        const name = message.content ? String(message.content).replace(/^wellcome,?\s*/i, '').replace(/!$/, '') : 'jettyCat 用户';
        setMyName(name);
        notify({ type: 'connection', connected: true, myId: Number(message.to) });
        flushQueue();
        return;
      }
      if (message.type !== 'private_message') return;
      const inner = parseContent(message.content);
      const sender = Number(inner && inner.from !== undefined ? inner.from : message.from);
      const receiver = Number(message.to);
      setConversations((prev) => {
        if (sender > 0 && !prev.some((item) => item.kind === 'user' && Number(item.id) === sender)) {
          const next = [{ kind: 'user', id: sender }, ...prev];
          saveConversations(next);
          return next;
        }
        return prev;
      });
      notify({ type: 'private_message', message, inner, sender, receiver });
      recordLastMessage('user', sender, (inner && inner.content) || '');
      if (activeKeyRef.current !== `user:${sender}`) {
        pushMessageNotice(sender, (inner && inner.content) || '你有一条新的私聊消息');
      }
    },
    [notify, pushMessageNotice, recordLastMessage]
  );

  const connect = useCallback(() => {
    aliveRef.current = true;
    const protocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    const ws = new WebSocket(protocol + window.location.host + WS_PATH);
    wsRef.current = ws;
    ws.onopen = () => {
      if (!aliveRef.current) return;
      setConnected(true);
      reconnectRef.current = 0;
      notify({ type: 'connection', connected: true });
      flushQueue();
    };
    ws.onmessage = (event) => handleMessage(event.data);
    ws.onerror = () => {
      try {
        ws.close();
      } catch (error) {
        /* ignore */
      }
    };
    ws.onclose = (event) => {
      if (!aliveRef.current) return;
      setConnected(false);
      notify({ type: 'connection', connected: false });
      if (event.code === 1008 || authFailedRef.current) {
        authFailedRef.current = true;
        showNotice('登录状态已失效', '请重新登录后继续使用');
        setTimeout(() => {
          window.location.href = '/login';
        }, 1200);
        return;
      }
      reconnectRef.current += 1;
      setTimeout(connect, Math.min(30000, 2000 * Math.pow(1.6, reconnectRef.current - 1)));
    };
  }, [handleMessage, notify, showNotice, flushQueue]);

  useEffect(() => {
    return () => {
      aliveRef.current = false;
      flushingRef.current = false;
      sendQueueRef.current.length = 0;
      try {
        wsRef.current && wsRef.current.close(1000, 'unmount');
      } catch (error) {
        /* ignore */
      }
      wsRef.current = null;
    };
  }, []);

  const subscribe = useCallback((fn) => {
    listenersRef.current.add(fn);
    return () => listenersRef.current.delete(fn);
  }, []);

  const sendMessage = useCallback(
    (to, content) => {
      const text = String(content == null ? '' : content).trim();
      if (!text) return false;
      sendQueueRef.current.push({ to: Number(to), content: text });
      flushQueue();
      return true;
    },
    [flushQueue]
  );

  const addConversation = useCallback((kind, id) => {
    setConversations((prev) => {
      if (prev.some((item) => item.kind === kind && Number(item.id) === Number(id))) return prev;
      const next = [...prev, { kind, id: Number(id) }];
      saveConversations(next);
      return next;
    });
  }, []);

  const setActiveKey = useCallback(
    (key) => {
      activeKeyRef.current = key;
      if (key) clearUnread(key);
    },
    [clearUnread]
  );

  const disconnect = useCallback(() => {
    aliveRef.current = false;
    flushingRef.current = false;
    sendQueueRef.current.length = 0;
    try {
      wsRef.current && wsRef.current.close(1000, 'disconnect');
    } catch (error) {
      /* ignore */
    }
    wsRef.current = null;
  }, []);

  const value = {
    connected,
    myId,
    myName,
    conversations,
    notices,
    unread,
    lastMessages,
    showNotice,
    dismissNotice,
    subscribe,
    sendMessage,
    addConversation,
    setActiveKey,
    clearUnread,
    recordLastMessage,
    connect,
    disconnect,
  };

  return <ChatContext.Provider value={value}>{children}</ChatContext.Provider>;
}
