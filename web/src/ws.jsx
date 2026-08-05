import React, { createContext, useContext, useCallback, useEffect, useRef, useState } from 'react';

const ChatContext = createContext(null);
export const useChat = () => useContext(ChatContext);

const WS_PATH = '/api/chat';
const ACTIVITY_STORE = 'jettycat.chat.activity';
const SORT_STORE = 'jettycat.chat.sortMode';

// 活跃来源注册表。新增活跃来源时, 在这里登记并在事件发生处调用
// `touchActivity(kind, id, source, ts)` 即可, 会话的 "最近活跃时间" 取该会话
// 所有来源时间戳的最大值。后端不维护该数据, 完全由前端在本地记录。
const ACTIVITY_SOURCES = {
  message: '最新消息',
  friend_added: '添加好友',
  group_joined: '加入群聊',
};

export function lastActiveOf(sources) {
  let last = 0;
  if (sources && typeof sources === 'object') {
    for (const key of Object.keys(sources)) {
      const ts = Number(sources[key]) || 0;
      if (ts > last) last = ts;
    }
  }
  return last;
}

function loadActivity() {
  try {
    const value = JSON.parse(localStorage.getItem(ACTIVITY_STORE) || '{}');
    return value && typeof value === 'object' ? value : {};
  } catch (error) {
    return {};
  }
}

function saveActivity(activity) {
  try {
    localStorage.setItem(ACTIVITY_STORE, JSON.stringify(activity));
  } catch (error) {
    /* storage is optional */
  }
}

function loadSortMode() {
  try {
    const value = localStorage.getItem(SORT_STORE);
    return value === 'active' || value === 'name' ? value : 'name';
  } catch (error) {
    return 'name';
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
  const [conversations, setConversations] = useState([]);
  const [activity, setActivity] = useState(loadActivity);
  const [sortMode, setSortMode] = useState(loadSortMode);
  const [contactsReady, setContactsReady] = useState(false);
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

  const touchActivity = useCallback((kind, id, source, ts) => {
    const key = `${kind}:${id}`;
    const time = Number(ts) || Date.now();
    setActivity((prev) => {
      const sources = prev[key] ? { ...prev[key] } : {};
      if (sources[source] && Number(sources[source]) >= time) return prev;
      sources[source] = time;
      const next = { ...prev, [key]: sources };
      saveActivity(next);
      return next;
    });
  }, []);

  const recordLastMessage = useCallback((kind, id, content, ts) => {
    setLastMessages((prev) => ({ ...prev, [`${kind}:${id}`]: content }));
    touchActivity(kind, id, 'message', ts);
  }, [touchActivity]);

  const changeSortMode = useCallback((mode) => {
    const next = mode === 'active' ? 'active' : 'name';
    setSortMode(next);
    try {
      localStorage.setItem(SORT_STORE, next);
    } catch (error) {
      /* storage is optional */
    }
  }, []);

  // 从服务端拉取好友/群聊列表并**整体替换**会话列表。
  // 本地以服务端为准: 服务端未返回的关系不会出现在列表中。
  const refreshLists = useCallback(async () => {
    let friends = [];
    let groups = [];
    try {
      const [friendResp, groupResp] = await Promise.all([
        fetch('/api/chat/friend_list', { credentials: 'include' }),
        fetch('/api/chat/group_list', { credentials: 'include' }),
      ]);
      const friendData = await friendResp.json();
      const groupData = await groupResp.json();
      if (friendData && friendData.status === 'success' && Array.isArray(friendData.friends)) {
        friends = friendData.friends;
      }
      if (groupData && groupData.status === 'success' && Array.isArray(groupData.groups)) {
        groups = groupData.groups;
      }
    } catch (error) {
      /* network errors keep the current list intact */
      return;
    }

    const conversations = [];
    for (const friend of friends) {
      const id = Number(friend.friend_id);
      if (!id) continue;
      conversations.push({
        kind: 'user',
        id,
        name: friend.nickname || friend.username || `用户 ${id}`,
        avatarUrl: friend.avatar_url || '',
      });
    }
    for (const group of groups) {
      const id = Number(group.group_id);
      if (!id) continue;
      conversations.push({
        kind: 'group',
        id,
        name: group.group_name || `群组 ${id}`,
        identity: group.identity || 'member',
        joinTime: Number(group.join_time) || 0,
      });
    }

    // 首次发现的关系(好友/群聊)记录一次活跃, 作为"最新添加/加入"的依据
    setActivity((prev) => {
      let next = prev;
      const now = Date.now();
      for (const item of conversations) {
        const key = `${item.kind}:${item.id}`;
        const source = item.kind === 'group' ? 'group_joined' : 'friend_added';
        if (prev[key] && prev[key][source]) continue;
        const joinedAt = item.kind === 'group' && item.joinTime ? item.joinTime * 1000 : now;
        next = { ...next, [key]: { ...(prev[key] || {}), [source]: joinedAt } };
      }
      if (next !== prev) saveActivity(next);
      return next;
    });

    setConversations(conversations);
    setContactsReady(true);
  }, []);

  // 调用服务端关系 API 的通用封装, 成功后自动刷新列表
  const apiMutation = useCallback(
    async (path, params) => {
      let data;
      try {
        const response = await fetch(`${path}?${new URLSearchParams(params)}`, { credentials: 'include' });
        data = await response.json();
      } catch (error) {
        return { status: 'error', error: '网络请求失败, 请稍后重试' };
      }
      if (data && data.status === 'success') {
        await refreshLists();
      }
      return data || { status: 'error', error: '服务端响应异常' };
    },
    [refreshLists]
  );

  const addFriend = useCallback((friendId) => apiMutation('/api/chat/add_friend', { friendId }), [apiMutation]);
  const removeFriend = useCallback((friendId) => apiMutation('/api/chat/remove_friend', { friendId }), [apiMutation]);
  const addGroup = useCallback((groupId) => apiMutation('/api/chat/add_group', { groupId }), [apiMutation]);
  const removeGroup = useCallback((groupId) => apiMutation('/api/chat/remove_group', { groupId }), [apiMutation]);

  // 连接成功后拉取一次好友/群聊列表
  useEffect(() => {
    if (myId != null) refreshLists();
  }, [myId, refreshLists]);

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
      notify({ type: 'private_message', message, inner, sender, receiver });
      recordLastMessage('user', sender, (inner && inner.content) || '', inner && inner.date);
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
      if (!aliveRef.current || wsRef.current !== ws) return;
      setConnected(true);
      reconnectRef.current = 0;
      notify({ type: 'connection', connected: true });
      flushQueue();
    };
    ws.onmessage = (event) => {
      if (wsRef.current !== ws) return;
      handleMessage(event.data);
    };
    ws.onerror = () => {
      try {
        ws.close();
      } catch (error) {
        /* ignore */
      }
    };
    ws.onclose = (event) => {
      if (wsRef.current !== ws) return;
      wsRef.current = null;
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
    activity,
    sortMode,
    contactsReady,
    notices,
    unread,
    lastMessages,
    showNotice,
    dismissNotice,
    subscribe,
    sendMessage,
    addFriend,
    removeFriend,
    addGroup,
    removeGroup,
    setActiveKey,
    clearUnread,
    recordLastMessage,
    touchActivity,
    changeSortMode,
    refreshLists,
    connect,
    disconnect,
  };

  return <ChatContext.Provider value={value}>{children}</ChatContext.Provider>;
}
