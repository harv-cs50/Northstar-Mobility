import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

const getSavedSession = () => {
  try { return JSON.parse(sessionStorage.getItem('vrms-session') || 'null'); }
  catch { return null; }
};

const API_BASE = import.meta.env.VITE_API_BASE_URL || '/api';

async function api(path, session, options = {}) {
  const headers = { ...(options.body ? { 'Content-Type': 'application/json' } : {}), ...options.headers };
  if (session?.sessionId) headers.Authorization = `Bearer ${session.sessionId}`;
  const response = await fetch(`${API_BASE}${path}`, { ...options, headers });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(payload.error || 'Request could not be completed');
  return payload;
}

const money = (value) => new Intl.NumberFormat('en-IN', {
  style: 'currency', currency: 'INR', maximumFractionDigits: 0,
}).format(Number(value || 0));

function vehiclePhotoFor(vehicle) {
  const key = `${vehicle.brand} ${vehicle.model}`.trim().toLowerCase().replace(/\s+/g, ' ');
  if (key === 'royal enfield classic 350') {
    return {
      url: 'https://thumb.wikimedia.org/wikipedia/commons/thumb/2/2b/2022Classic350.jpg/960px-2022Classic350.jpg',
      source: 'https://commons.wikimedia.org/wiki/File:2022Classic350.jpg',
      author: 'Billyinthedarbies',
    };
  }
  if (key === 'toyota camry') {
    return {
      url: 'https://upload.wikimedia.org/wikipedia/commons/thumb/e/e8/Toyota_Camry_Sedan.jpg/1280px-Toyota_Camry_Sedan.jpg',
      source: 'https://commons.wikimedia.org/wiki/File:Toyota_Camry_Sedan.jpg',
      author: 'Bene Riobó',
    };
  }
  if (key === 'hyundai creta 1.5 plus 2024') {
    return {
      url: 'https://thumb.wikimedia.org/wikipedia/commons/thumb/3/3d/Hyundai_Creta_1.5_Plus_2024.jpg/960px-Hyundai_Creta_1.5_Plus_2024.jpg',
      source: 'https://commons.wikimedia.org/wiki/File:Hyundai_Creta_1.5_Plus_2024.jpg',
      author: 'RL GNZLZ',
    };
  }
  if (key === 'suzuki swift 2024') {
    return {
      url: 'https://thumb.wikimedia.org/wikipedia/commons/thumb/e/e7/2024_Suzuki_Swift.jpg/960px-2024_Suzuki_Swift.jpg',
      source: 'https://commons.wikimedia.org/wiki/File:2024_Suzuki_Swift.jpg',
      author: 'Calreyn88',
    };
  }
  return null;
}

function App() {
  const [session, setSession] = useState(getSavedSession);
  const [theme, setTheme] = useState(() => {
    try { return localStorage.getItem('northstar-theme') === 'dark' ? 'dark' : 'light'; }
    catch { return 'light'; }
  });
  const [vehicles, setVehicles] = useState([]);
  const [bookings, setBookings] = useState([]);
  const [payments, setPayments] = useState([]);
  const [invoices, setInvoices] = useState([]);
  const [search, setSearch] = useState('');
  const [authMode, setAuthMode] = useState('login');
  const [notice, setNotice] = useState('');
  const [busy, setBusy] = useState(false);
  const [selected, setSelected] = useState(null);
  const [bookingDates, setBookingDates] = useState({ startDate: '', endDate: '' });
  const [paymentMethod, setPaymentMethod] = useState('Card');
  const [showBootstrap, setShowBootstrap] = useState(false);
  const [adminTab, setAdminTab] = useState('fleet');
  const [adminData, setAdminData] = useState({ vehicles: [], bookings: [], maintenance: [], rentals: [], invoices: [] });

  const loadVehicles = useCallback(async () => {
    try { setVehicles(await api('/vehicles')); }
    catch (error) { setNotice(error.message); }
  }, []);

  const loadCustomerData = useCallback(async (activeSession) => {
    if (!activeSession) return;
    try {
      const [nextBookings, nextPayments, nextInvoices] = await Promise.all([
        api('/bookings', activeSession), api('/payments', activeSession), api('/invoices', activeSession),
      ]);
      setBookings(nextBookings); setPayments(nextPayments); setInvoices(nextInvoices);
    } catch (error) { setNotice(error.message); }
  }, []);

  const loadAdminData = useCallback(async () => {
    if (session?.role !== 'ADMIN') return;
    try {
      const [nextVehicles, nextBookings, maintenance, rentals, invoices] = await Promise.all([
        api('/admin/vehicles', session), api('/admin/bookings', session),
        api('/admin/maintenance', session), api('/admin/rentals', session), api('/invoices', session),
      ]);
      setAdminData({ vehicles: nextVehicles, bookings: nextBookings, maintenance, rentals, invoices });
    } catch (error) { setNotice(error.message); }
  }, [session]);

  useEffect(() => { loadVehicles(); }, [loadVehicles]);
  useEffect(() => { loadCustomerData(session); if (session?.role === 'ADMIN') loadAdminData(); }, [session, loadCustomerData, loadAdminData]);
  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    document.querySelector('meta[name="theme-color"]')?.setAttribute(
      'content', theme === 'dark' ? '#151a18' : '#f7f8f6',
    );
    try { localStorage.setItem('northstar-theme', theme); } catch { /* theme still works for this page view */ }
  }, [theme]);

  const filteredVehicles = useMemo(() => {
    const needle = search.trim().toLowerCase();
    if (!needle) return vehicles;
    return vehicles.filter((vehicle) => `${vehicle.brand} ${vehicle.model} ${vehicle.type}`.toLowerCase().includes(needle));
  }, [vehicles, search]);

  function saveSession(value) {
    sessionStorage.setItem('vrms-session', JSON.stringify(value));
    setSession(value);
    setNotice(`Welcome, ${value.name}`);
  }

  async function submitAuth(event) {
    event.preventDefault(); setBusy(true); setNotice('');
    const form = new FormData(event.currentTarget);
    try {
      if (authMode === 'register') {
        await api('/auth/register', null, { method: 'POST', body: JSON.stringify({
          userId: form.get('userId'), name: form.get('name'), email: form.get('email'), password: form.get('password'),
        }) });
        setNotice('Account created. Sign in to continue.'); setAuthMode('login');
      } else {
        const result = await api('/auth/login', null, { method: 'POST', body: JSON.stringify({ email: form.get('email'), password: form.get('password') }) });
        saveSession(result);
      }
    } catch (error) { setNotice(error.message); }
    finally { setBusy(false); }
  }

  async function logout() {
    try { await api('/auth/logout', session, { method: 'POST' }); } catch { /* local sign-out still clears an expired session */ }
    sessionStorage.removeItem('vrms-session'); setSession(null); setBookings([]); setPayments([]); setInvoices([]); setNotice('You are signed out.');
  }

  async function createBooking(event) {
    event.preventDefault(); if (!session) { setNotice('Sign in as a customer to book a vehicle.'); setSelected(null); return; }
    setBusy(true);
    try {
      await api('/bookings', session, { method: 'POST', body: JSON.stringify({ vehicleId: selected.id, ...bookingDates }) });
      setNotice('Booking confirmed.'); setSelected(null); await loadCustomerData(session);
    } catch (error) { setNotice(error.message); }
    finally { setBusy(false); }
  }

  async function pay(bookingId) {
    setBusy(true);
    try {
      const payment = await api('/payments', session, { method: 'POST', body: JSON.stringify({ bookingId, method: paymentMethod }) });
      const deliveryMessage = payment.emailStatus === 'SENT'
        ? `Invoice ${payment.invoiceId} was emailed to your registered address.`
        : payment.emailStatus === 'FAILED'
          ? `Invoice ${payment.invoiceId} is saved, but email delivery failed. An administrator can retry it.`
          : `Invoice ${payment.invoiceId} is saved. Email delivery status: ${payment.emailStatus}.`;
      setNotice(`Payment simulated successfully. ${deliveryMessage}`);
      await loadCustomerData(session);
    } catch (error) { setNotice(error.message); }
    finally { setBusy(false); }
  }

  async function cancelBooking(bookingId) {
    try {
      await api(`/bookings/${encodeURIComponent(bookingId)}/cancel`, session, { method: 'POST' });
      setNotice('Booking cancelled.'); await loadCustomerData(session); await loadAdminData();
    } catch (error) { setNotice(error.message); }
  }

  async function createVehicle(event) {
    event.preventDefault(); const form = new FormData(event.currentTarget);
    try {
      await api('/admin/vehicles', session, { method: 'POST', body: JSON.stringify({
        id: form.get('id'), brand: form.get('brand'), model: form.get('model'), type: form.get('type'), dailyRate: Number(form.get('dailyRate')),
      }) });
      event.currentTarget.reset(); setNotice('Vehicle saved.'); await loadAdminData(); await loadVehicles();
    } catch (error) { setNotice(error.message); }
  }

  async function createAdmin(event) {
    event.preventDefault(); const form = new FormData(event.currentTarget);
    try {
      await api('/admin/users', session, { method: 'POST', body: JSON.stringify({
        userId: form.get('userId'), name: form.get('name'), email: form.get('email'), password: form.get('password'),
      }) });
      event.currentTarget.reset(); setNotice('Administrator account created.');
    } catch (error) { setNotice(error.message); }
  }

  async function retryInvoiceEmail(invoiceId) {
    try {
      const result = await api(`/admin/invoices/${encodeURIComponent(invoiceId)}/retry-email`, session, { method: 'POST' });
      setNotice(result.message || `Invoice email status: ${result.emailStatus}`);
      await loadAdminData();
    } catch (error) { setNotice(error.message); }
  }

  async function bootstrapAdmin(event) {
    event.preventDefault(); const form = new FormData(event.currentTarget);
    try {
      const token = String(form.get('token') || '');
      const result = await api('/auth/bootstrap-admin', null, { method: 'POST', headers: { 'X-Admin-Bootstrap-Token': token }, body: JSON.stringify({
        userId: form.get('userId'), name: form.get('name'), email: form.get('email'), password: form.get('password'),
      }) });
      saveSession(result); setShowBootstrap(false); setNotice('Initial administrator provisioned. Store the bootstrap token securely; the database setup is now closed.');
    } catch (error) { setNotice(error.message); }
  }

  return (
    <div className="min-h-screen bg-paper text-ink">
      <header className="topbar">
        <a className="brand" href="#top" aria-label="Northstar home"><span className="brand-mark">N</span><span>NORTHSTAR<span className="brand-sub">MOBILITY</span></span></a>
        <nav className="navlinks"><a href="#fleet">Fleet</a>{session && <a href="#account">{session.role === 'ADMIN' ? 'Dashboard' : 'My bookings'}</a>}<a href="#about">Our promise</a></nav>
        <div className="nav-actions"><button className="theme-toggle" type="button" onClick={() => setTheme((current) => current === 'dark' ? 'light' : 'dark')} aria-label={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'} aria-pressed={theme === 'dark'} title={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}><span aria-hidden="true">{theme === 'dark' ? '☼' : '☾'}</span></button>{session ? <><span className="user-chip">{session.name} <b>{session.role}</b></span><button className="button button-quiet" onClick={logout}>Sign out</button></> : <><button className="button button-quiet" onClick={() => { setAuthMode('login'); document.getElementById('account')?.scrollIntoView({ behavior: 'smooth' }); }}>Sign in</button><button className="button button-dark" onClick={() => { setAuthMode('register'); document.getElementById('account')?.scrollIntoView({ behavior: 'smooth' }); }}>Create account</button></>}</div>
      </header>

      {notice && <button className="notice" onClick={() => setNotice('')} title="Dismiss message">{notice}<span>×</span></button>}

      <main id="top">
        <section className="hero wrap">
          <div className="hero-copy"><p className="eyebrow"><span className="eyebrow-dot" /> YOUR NEXT DRIVE, MADE SIMPLE</p><h1>Make room<br />for <em>the journey.</em></h1><p className="hero-intro">Find a car that fits your plans. Clear pricing, thoughtful service, and the freedom to go a little further.</p><a href="#fleet" className="button button-lime">Explore the fleet <span>↗</span></a><div className="hero-proof"><div className="avatar-stack"><i>A</i><i>M</i><i>R</i></div><span><strong>4.9/5</strong><br />trusted by local drivers</span></div></div>
          <div className="hero-visual"><div className="sun-disc"/><div className="hero-road"/><div className="hero-car"><div className="car-roof"/><div className="car-body"><span/><span/></div><div className="wheel wheel-left"/><div className="wheel wheel-right"/></div><div className="floating-card"><span className="float-icon">✦</span><div><b>Easy from pickup</b><small>to wherever you're going</small></div><span className="float-arrow">↗</span></div><div className="hero-note">CURATED FOR<br />THE OPEN ROAD</div></div>
        </section>

        <section className="trust-strip"><div className="wrap trust-inner"><span>THE NORTHSTAR DIFFERENCE</span><b>Flexible pickup</b><b>Upfront daily rates</b><b>Support when you need it</b><b>Well-kept vehicles</b></div></section>

        <section id="fleet" className="fleet-section wrap">
          <div className="section-heading"><div><p className="eyebrow">THE FLEET</p><h2>Find your kind of <em>drive.</em></h2></div><label className="searchbox"><span>⌕</span><input value={search} onChange={(event) => setSearch(event.target.value)} placeholder="Search make or style" /></label></div>
          <div className="filters"><span>{filteredVehicles.length} available vehicles</span><span className="filter-label">All vehicles <b>⌄</b></span></div>
          {filteredVehicles.length ? <div className="vehicle-grid">{filteredVehicles.map((vehicle) => {
            const photo = vehiclePhotoFor(vehicle);
            return <article className="vehicle-card" key={vehicle.id}><div className="vehicle-art vehicle-photo-art">
              <div className="vehicle-photo-placeholder"><span>Fleet vehicle</span><strong>{vehicle.brand} {vehicle.model}</strong><small>Verified photo unavailable</small></div>
              {photo && <img className="vehicle-photo" src={photo.url} alt={`${vehicle.brand} ${vehicle.model}`} loading="lazy" referrerPolicy="no-referrer" onError={(event) => { event.currentTarget.hidden = true; }} />}
              <div className="vehicle-photo-shade" aria-hidden="true" />
              <span className="vehicle-type">{vehicle.type}</span>
              {photo && <span className="photo-credit">Photo: <a href={photo.source} target="_blank" rel="noreferrer">{photo.author}</a> · <a href="https://creativecommons.org/licenses/by-sa/4.0/" target="_blank" rel="noreferrer">CC BY-SA 4.0</a></span>}
              <span className="vehicle-id">{vehicle.id}</span>
            </div><div className="vehicle-info"><div><h3>{vehicle.brand} {vehicle.model}</h3><p>{vehicle.type} <span>·</span> Available now</p></div><div className="rate">{money(vehicle.dailyRate)}<small>/ day</small></div></div><button className="button button-outline book-button" onClick={() => { setSelected(vehicle); setBookingDates({ startDate: '', endDate: '' }); }}>Reserve this car <span>↗</span></button></article>;
          })}</div> : <div className="empty-state">No available vehicles match that search. Try another make or vehicle type.</div>}
          <p className="availability-note">Availability reflects the fleet's current status. Your dates are confirmed when the booking is accepted.</p>
        </section>

        <section id="about" className="promise-section"><div className="wrap promise-grid"><div><p className="eyebrow">GOOD TO GO, YOUR WAY</p><h2>Less planning.<br /><em>More living.</em></h2><p>Every part of the rental should feel straightforward. Pick your dates, choose your car, and get on with what matters.</p><a href="#fleet" className="text-link">Meet the fleet <span>↗</span></a></div><div className="promise-list"><div><b>01</b><span><strong>Choose with confidence</strong><small>Browse real vehicles with a clear daily rate.</small></span><span>↗</span></div><div><b>02</b><span><strong>Book in a few steps</strong><small>Your dates and booking history stay in one place.</small></span><span>↗</span></div><div><b>03</b><span><strong>Take the scenic route</strong><small>Enjoy straightforward pickup and helpful support.</small></span><span>↗</span></div></div></div></section>

        <section id="account" className="account-section wrap">
          {session?.role === 'ADMIN' ? <AdminDashboard session={session} data={adminData} tab={adminTab} setTab={setAdminTab} onCreateVehicle={createVehicle} onCreateAdmin={createAdmin} onCancel={cancelBooking} onRetryInvoice={retryInvoiceEmail} refresh={loadAdminData} /> : session ? <CustomerAccount bookings={bookings} payments={payments} invoices={invoices} onPay={pay} busy={busy} method={paymentMethod} setMethod={setPaymentMethod} onCancel={cancelBooking} /> : <div className="account-layout"><div className="account-copy"><p className="eyebrow">YOUR NORTHSTAR ACCOUNT</p><h2>Good trips start<br />with <em>one step.</em></h2><p>Sign in to manage your reservations, or create an account to get moving.</p><button className="text-link" onClick={() => setShowBootstrap(true)}>First-time administrator setup <span>↗</span></button></div><form className="auth-card" onSubmit={submitAuth}><div className="auth-tabs"><button type="button" className={authMode === 'login' ? 'active' : ''} onClick={() => setAuthMode('login')}>Sign in</button><button type="button" className={authMode === 'register' ? 'active' : ''} onClick={() => setAuthMode('register')}>Create account</button></div><h3>{authMode === 'login' ? 'Welcome back.' : 'Join Northstar.'}</h3><p>{authMode === 'login' ? 'Use your email and password to continue.' : 'A few details and you’re ready to book.'}</p>{authMode === 'register' && <><label>Account ID<input name="userId" required placeholder="e.g. CUST-204" /></label><label>Full name<input name="name" required autoComplete="name" /></label></>}<label>Email address<input name="email" type="email" required autoComplete="email" /></label><label>Password<input name="password" type="password" minLength="8" required autoComplete={authMode === 'login' ? 'current-password' : 'new-password'} /></label><button className="button button-dark full" disabled={busy}>{busy ? 'Please wait…' : authMode === 'login' ? 'Sign in securely' : 'Create customer account'} <span>↗</span></button><small className="auth-foot">Passwords are protected with Argon2id. Customer accounts cannot choose an administrator role.</small></form></div>}
        </section>
      </main>

      <footer className="footer"><div className="wrap footer-inner"><a className="brand" href="#top"><span className="brand-mark">N</span><span>NORTHSTAR<span className="brand-sub">MOBILITY</span></span></a><span>Make room for the journey.</span><span>© 2026 Northstar Rentals · VRMS</span></div></footer>

      {session?.role !== 'ADMIN' && <SupportChat session={session} />}

      {selected && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget) setSelected(null); }}><form className="modal-card" onSubmit={createBooking}><button className="modal-close" type="button" onClick={() => setSelected(null)}>×</button><p className="eyebrow">RESERVE YOUR VEHICLE</p><h2>{selected.brand} {selected.model}</h2><p>{money(selected.dailyRate)} per day · {selected.type}</p><label>Pickup date<input type="date" required value={bookingDates.startDate} onChange={(e) => setBookingDates({ ...bookingDates, startDate: e.target.value })} /></label><label>Return date<input type="date" min={bookingDates.startDate} required value={bookingDates.endDate} onChange={(e) => setBookingDates({ ...bookingDates, endDate: e.target.value })} /></label><button className="button button-dark full" disabled={busy}>{session ? 'Confirm booking' : 'Sign in to continue'} <span>↗</span></button><small>Booking dates are checked by the server for real calendar dates and overlapping reservations.</small></form></div>}

      {showBootstrap && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget) setShowBootstrap(false); }}><form className="modal-card" onSubmit={bootstrapAdmin}><button className="modal-close" type="button" onClick={() => setShowBootstrap(false)}>×</button><p className="eyebrow">ONE-TIME SETUP</p><h2>Provision the first admin.</h2><p>The server must have VRMS_ADMIN_BOOTSTRAP_TOKEN set. Only one bootstrap request can succeed.</p><label>Bootstrap token<input name="token" type="password" required autoComplete="off" /></label><label>Admin ID<input name="userId" required /></label><label>Name<input name="name" required /></label><label>Email<input name="email" type="email" required /></label><label>Password<input name="password" type="password" minLength="8" required /></label><button className="button button-dark full" disabled={busy}>{busy ? 'Provisioning…' : 'Create first administrator'} <span>↗</span></button></form></div>}
    </div>
  );
}

const suggestedQuestions = [
  'Which vehicles are available?',
  'What is your cheapest vehicle?',
  'Calculate the cost for five days.',
  'How can I find my booking?',
  'Where can I find my invoice?',
];

function newWelcomeMessage(session) {
  const text = session?.role === 'CUSTOMER'
    ? 'Hello! I can help with the current fleet and look up your bookings, payment status, and invoice delivery.'
    : 'Hello! Ask me about the currently available fleet, daily rates, or how Northstar works.';
  return { id: `welcome-${Date.now()}`, role: 'assistant', text, time: new Date() };
}

function SupportChat({ session }) {
  const [open, setOpen] = useState(false);
  const [messages, setMessages] = useState(() => [newWelcomeMessage(session)]);
  const [input, setInput] = useState('');
  const [busy, setBusy] = useState(false);
  const listRef = useRef(null);
  const requestId = useRef(0);
  const identityKey = session?.role === 'CUSTOMER' ? session.userId : 'guest';

  useEffect(() => {
    requestId.current += 1;
    setMessages([newWelcomeMessage(session)]);
    setInput('');
    setBusy(false);
  }, [identityKey]);

  useEffect(() => {
    listRef.current?.scrollTo({ top: listRef.current.scrollHeight, behavior: 'smooth' });
  }, [messages, busy, open]);

  function clearHistory() {
    requestId.current += 1;
    setBusy(false);
    setMessages([newWelcomeMessage(session)]);
    setInput('');
  }

  async function sendMessage(rawMessage) {
    const message = String(rawMessage || '').trim();
    if (!message || busy || message.length > 1000) return;
    if (new TextEncoder().encode(message).length > 1000) {
      setMessages((current) => [...current, {
        id: `error-${Date.now()}`, role: 'error',
        text: 'Please shorten this message. The support limit is 1,000 UTF-8 bytes.',
        time: new Date(),
      }]);
      return;
    }
    const currentId = ++requestId.current;
    const sensitiveTurn = /\b(booking|reservation|payment|invoice|history|trip|rental|receipt|password|secret|token|api key)\b|@/i.test(message);
    const history = messages
      .filter((item) => !item.sensitive && (item.role === 'user' || item.role === 'assistant'))
      .slice(-6)
      .map((item) => `${item.role === 'user' ? 'Customer' : 'Assistant'}: ${Array.from(item.text).slice(-150).join('')}`)
      .join('\n');
    setMessages((current) => [...current, {
      id: `user-${currentId}`, role: 'user', text: message, time: new Date(), sensitive: sensitiveTurn,
    }]);
    setInput('');
    setBusy(true);
    try {
      const response = await api('/chat', session, {
        method: 'POST', body: JSON.stringify({ message, history }),
      });
      if (requestId.current === currentId) {
        setMessages((current) => [...current, {
          id: `assistant-${currentId}`, role: 'assistant',
          text: response.reply, time: new Date(), sensitive: sensitiveTurn,
        }]);
      }
    } catch (error) {
      if (requestId.current === currentId) {
        setMessages((current) => [...current, {
          id: `error-${currentId}`, role: 'error',
          text: error.message || 'Support is temporarily unavailable. Please try again.',
          time: new Date(),
        }]);
      }
    } finally {
      if (requestId.current === currentId) setBusy(false);
    }
  }

  function retryLastMessage() {
    const lastUser = [...messages].reverse().find((item) => item.role === 'user');
    if (lastUser) sendMessage(lastUser.text);
  }

  function submit(event) {
    event.preventDefault();
    sendMessage(input);
  }

  return <div className="support-chat">
    {open && <section className="chat-panel" role="dialog" aria-label="Northstar customer support chat">
      <header className="chat-header">
        <div className="chat-brand-mark">N</div>
        <div className="chat-header-copy"><strong>Northstar Assist</strong><span><i /> Here to help</span></div>
        <button className="chat-header-action" type="button" onClick={clearHistory} aria-label="Clear chat history" title="Clear chat history">↺</button>
        <button className="chat-header-action" type="button" onClick={() => setOpen(false)} aria-label="Close chat">×</button>
      </header>
      <div className="chat-disclaimer">Read-only support · No bookings or payments are changed here</div>
      <div className="chat-messages" ref={listRef} aria-live="polite">
        {messages.map((item) => <div className={`chat-message-row ${item.role}`} key={item.id}>
          <div className={`chat-bubble ${item.role}`}>
            <p>{item.text}</p>
            <time dateTime={item.time.toISOString()}>{item.time.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' })}</time>
            {item.role === 'error' && <button className="chat-retry" type="button" onClick={retryLastMessage}>Try again</button>}
          </div>
        </div>)}
        {messages.length === 1 && <div className="chat-suggestions"><span>QUICK QUESTIONS</span>{suggestedQuestions.map((question) => <button type="button" key={question} onClick={() => sendMessage(question)} disabled={busy}>{question}<b>↗</b></button>)}</div>}
        {busy && <div className="chat-message-row assistant"><div className="chat-bubble assistant chat-typing" aria-label="Assistant is typing"><span /><span /><span /></div></div>}
      </div>
      <form className="chat-composer" onSubmit={submit}>
        <input value={input} onChange={(event) => setInput(event.target.value)} maxLength={1000} placeholder="Ask Northstar Assist…" aria-label="Your message" disabled={busy} />
        <button type="submit" aria-label="Send message" disabled={busy || !input.trim()}>↑</button>
      </form>
      <div className="chat-footnote">Don't enter passwords or payment details. General messages go to Gemini; account records stay on the server.</div>
    </section>}
    <button className={`chat-launcher ${open ? 'active' : ''}`} type="button" onClick={() => setOpen((current) => !current)} aria-label={open ? 'Close Northstar support chat' : 'Open Northstar support chat'}>
      <span aria-hidden="true">{open ? '×' : '✦'}</span>{!open && <b>Ask us</b>}
    </button>
  </div>;
}

function CustomerAccount({ bookings, payments, invoices, onPay, busy, method, setMethod, onCancel }) {
  return <div className="dashboard">
    <div className="dashboard-heading"><div><p className="eyebrow">YOUR ACCOUNT</p><h2>Trips, all <em>in one place.</em></h2></div><a className="button button-dark" href="#fleet">Book a vehicle <span>↗</span></a></div>
    <div className="stat-row"><Stat label="Reservations" value={bookings.length} /><Stat label="Payments" value={payments.length} /><Stat label="Invoices" value={invoices.length} /></div>
    <div className="table-card"><div className="table-title"><h3>My bookings</h3><span>Booking history</span></div>{bookings.length ? <div className="table-scroll"><table><thead><tr><th>Vehicle</th><th>Dates</th><th>Status</th><th>Payment / invoice</th><th /></tr></thead><tbody>{bookings.map((booking) => { const paid = payments.some((payment) => payment.bookingId === booking.bookingId && payment.status === 'Successful'); const invoice = invoices.find((item) => item.bookingId === booking.bookingId); return <tr key={booking.bookingId}><td><strong>{booking.vehicleId}</strong><small>{booking.bookingId}</small></td><td>{booking.startDate}<small>to {booking.endDate}</small></td><td><span className={`status ${booking.status.toLowerCase()}`}>{booking.status}</span></td><td>{paid ? <span>Paid · {invoice ? money(invoice.totalAmount) : 'invoice ready'}{invoice && <small>Invoice email: {invoice.emailStatus}</small>}</span> : booking.status !== 'CANCELLED' ? <div className="pay-controls"><select value={method} onChange={(e) => setMethod(e.target.value)}><option>Card</option><option>UPI</option><option>Cash</option></select><button className="mini-button" disabled={busy} onClick={() => onPay(booking.bookingId)}>Pay</button></div> : '—'}</td><td>{booking.status !== 'CANCELLED' && <button className="link-button" onClick={() => onCancel(booking.bookingId)}>Cancel</button>}</td></tr>; })}</tbody></table></div> : <div className="empty-inline">No bookings yet. Explore the fleet to plan your first trip.</div>}</div>
    <div className="fine-print">Payments are simulated for demonstration. No card data is collected or sent to a payment provider.</div>
  </div>;
}

function AdminDashboard({ session, data, tab, setTab, onCreateVehicle, onCreateAdmin, onCancel, onRetryInvoice, refresh }) {
  const tabs = [['fleet', 'Fleet'], ['bookings', 'Bookings'], ['maintenance', 'Maintenance'], ['rentals', 'Rentals'], ['invoices', 'Invoices'], ['admins', 'Admin access']];
  return <div className="dashboard">
    <div className="dashboard-heading"><div><p className="eyebrow">ADMIN WORKSPACE</p><h2>Fleet at a <em>glance.</em></h2></div><button className="button button-outline" onClick={refresh}>Refresh data ↻</button></div>
    <div className="stat-row"><Stat label="Vehicles" value={data.vehicles.length} /><Stat label="Bookings" value={data.bookings.length} /><Stat label="In maintenance" value={data.maintenance.filter((item) => item.status === 'SCHEDULED').length} /><Stat label="Active rentals" value={data.rentals.filter((item) => !item.returned).length} /></div>
    <div className="admin-tabs">{tabs.map(([key, label]) => <button className={tab === key ? 'active' : ''} key={key} onClick={() => setTab(key)}>{label}</button>)}</div>
    {tab === 'fleet' && <div className="admin-split"><div className="table-card"><div className="table-title"><h3>Fleet inventory</h3></div><div className="table-scroll"><table><thead><tr><th>Vehicle</th><th>Type</th><th>Daily rate</th><th>Availability</th></tr></thead><tbody>{data.vehicles.map((v) => <tr key={v.id}><td><strong>{v.brand} {v.model}</strong><small>{v.id}</small></td><td>{v.type}</td><td>{money(v.dailyRate)}</td><td><span className={`status ${v.available ? 'confirmed' : 'cancelled'}`}>{v.available ? 'Available' : 'Unavailable'}</span></td></tr>)}</tbody></table></div></div><form className="form-card" onSubmit={onCreateVehicle}><h3>Add or update a vehicle</h3><label>Vehicle ID<input name="id" required /></label><div className="form-pair"><label>Brand<input name="brand" required /></label><label>Model<input name="model" required /></label></div><div className="form-pair"><label>Type<select name="type"><option>Car</option><option>SUV</option><option>Van</option><option>Bike</option></select></label><label>Daily rate<input name="dailyRate" type="number" min="1" step="0.01" required /></label></div><button className="button button-dark full">Save vehicle <span>↗</span></button><small>Saving an existing vehicle ID updates its descriptive fields through the repository.</small></form></div>}
    {tab === 'bookings' && <div className="table-card"><div className="table-title"><h3>All bookings</h3></div><div className="table-scroll"><table><thead><tr><th>Booking</th><th>Customer</th><th>Vehicle</th><th>Dates</th><th>Status</th><th /></tr></thead><tbody>{data.bookings.map((b) => <tr key={b.bookingId}><td>{b.bookingId}</td><td>{b.customerId}</td><td>{b.vehicleId}</td><td>{b.startDate}<small>to {b.endDate}</small></td><td><span className={`status ${b.status.toLowerCase()}`}>{b.status}</span></td><td>{b.status !== 'CANCELLED' && <button className="link-button" onClick={() => onCancel(b.bookingId)}>Cancel</button>}</td></tr>)}</tbody></table></div></div>}
    {tab === 'maintenance' && <div className="table-card"><div className="table-title"><h3>Maintenance overview</h3><span>Scheduling and completion remain in the backend workflow.</span></div><div className="table-scroll"><table><thead><tr><th>Record</th><th>Vehicle</th><th>Service date</th><th>Description</th><th>Cost</th><th>Status</th></tr></thead><tbody>{data.maintenance.map((m) => <tr key={m.maintenanceId}><td>{m.maintenanceId}</td><td>{m.vehicleId}</td><td>{m.serviceDate}</td><td>{m.description}</td><td>{money(m.maintenanceCost)}</td><td><span className={`status ${m.status.toLowerCase()}`}>{m.status}</span></td></tr>)}</tbody></table></div></div>}
    {tab === 'rentals' && <div className="table-card"><div className="table-title"><h3>Rental records</h3><span>Existing rental and return records</span></div><div className="table-scroll"><table><thead><tr><th>Rental</th><th>Booking</th><th>Planned return</th><th>Actual return</th><th>Late fee/day</th><th>Status</th></tr></thead><tbody>{data.rentals.map((r) => <tr key={r.rentalId}><td>{r.rentalId}</td><td>{r.bookingId}</td><td>{r.plannedReturnDate}</td><td>{r.actualReturnDate || '—'}</td><td>{money(r.dailyLateFee)}</td><td>{r.returned ? 'Returned' : 'Active'}</td></tr>)}</tbody></table></div></div>}
    {tab === 'invoices' && <div className="table-card"><div className="table-title"><h3>Invoice email delivery</h3><span>Failed or pending invoice emails can be retried.</span></div>{data.invoices.length ? <div className="table-scroll"><table><thead><tr><th>Invoice</th><th>Booking</th><th>Total</th><th>Email status</th><th>Attempts</th><th /></tr></thead><tbody>{data.invoices.map((invoice) => <tr key={invoice.invoiceId}><td>{invoice.invoiceId}</td><td>{invoice.bookingId}</td><td>{money(invoice.totalAmount)}</td><td><span className={`status ${invoice.emailStatus.toLowerCase()}`}>{invoice.emailStatus}</span></td><td>{invoice.emailAttempts}</td><td>{['FAILED', 'PENDING'].includes(invoice.emailStatus) && <button className="link-button" onClick={() => onRetryInvoice(invoice.invoiceId)}>Retry email</button>}</td></tr>)}</tbody></table></div> : <div className="empty-inline">No invoices have been issued yet.</div>}</div>}
    {tab === 'admins' && <div className="admin-split"><div className="table-card access-note"><p className="eyebrow">ROLE-BASED ACCESS</p><h3>Provision administrators deliberately.</h3><p>The current schema stores ADMIN or CUSTOMER. Administrator creation requires this authenticated admin session; access level is never accepted from the browser.</p><span>Signed in as {session.name} · {session.role}</span></div><form className="form-card" onSubmit={onCreateAdmin}><h3>Create admin account</h3><label>Admin ID<input name="userId" required /></label><label>Name<input name="name" required /></label><label>Email<input name="email" type="email" required /></label><label>Temporary password<input name="password" type="password" minLength="8" required /></label><button className="button button-dark full">Create administrator <span>↗</span></button></form></div>}
  </div>;
}
function Stat({ label, value }) { return <div className="stat-card"><span>{label}</span><strong>{value}</strong><i>↗</i></div>; }

export default App;
