import React from 'react';
import { createRoot } from 'react-dom/client';
import '@chatui/core/dist/index.css';
import './admin.css';
import AdminApp from './AdminApp';

createRoot(document.getElementById('admin-root')).render(
  <React.StrictMode>
    <AdminApp />
  </React.StrictMode>
);
