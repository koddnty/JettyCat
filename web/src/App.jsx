import React from 'react';
import { Routes, Route, Navigate } from 'react-router-dom';
import { ChatProvider } from './ws';
import LoginPage from './pages/Login';
import MainPage from './pages/Main';

export default function App() {
  return (
    <ChatProvider>
      <Routes>
        <Route path="/" element={<Navigate to="/login" replace />} />
        <Route path="/pages/login.html" element={<Navigate to="/login" replace />} />
        <Route path="/pages/main.html" element={<Navigate to="/main" replace />} />
        <Route path="/login" element={<LoginPage />} />
        <Route path="/main" element={<MainPage />} />
        <Route path="*" element={<Navigate to="/login" replace />} />
      </Routes>
    </ChatProvider>
  );
}
