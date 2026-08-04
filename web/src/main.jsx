import React from 'react';
import { createRoot } from 'react-dom/client';
import { BrowserRouter } from 'react-router-dom';
import '@chatui/core/dist/index.css';
import './theme.css';
import App from './App';
import { IconSprite } from './components/IconSprite';

createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <BrowserRouter>
      <IconSprite />
      <App />
    </BrowserRouter>
  </React.StrictMode>
);
