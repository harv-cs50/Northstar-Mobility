export class ApiError extends Error {
  constructor(message, status) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
  }
}

function notifySessionExpired() {
  if (typeof window === 'undefined') return;
  window.dispatchEvent(new Event('vrms:session-expired'));
}

export function createVrmsApi(baseUrl = '/api', fetchImplementation = globalThis.fetch) {
  const base = String(baseUrl || '/api').trim().replace(/\/+$/, '') || '/api';
  if (typeof fetchImplementation !== 'function') {
    throw new TypeError('A Fetch-compatible function is required');
  }

  async function request(path, session = null, options = {}) {
    const hasBody = Object.hasOwn(options, 'body') && options.body !== undefined;
    const headers = {
      ...(hasBody ? { 'Content-Type': 'application/json' } : {}),
      ...options.headers,
    };
    if (session?.sessionId) headers.Authorization = `Bearer ${session.sessionId}`;

    let response;
    try {
      response = await fetchImplementation(`${base}${path}`, {
        ...options,
        headers,
        ...(hasBody && typeof options.body !== 'string'
          ? { body: JSON.stringify(options.body) }
          : {}),
      });
    } catch {
      throw new ApiError('Could not reach the Northstar service. Check your connection and try again.', 0);
    }

    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      if (response.status === 401 && session?.sessionId) notifySessionExpired();
      throw new ApiError(
        typeof payload?.error === 'string' ? payload.error : 'Request could not be completed',
        response.status,
      );
    }
    return payload;
  }

  return {
    registerCustomer: (details) => request('/auth/register', null, { method: 'POST', body: details }),
    login: (credentials) => request('/auth/login', null, { method: 'POST', body: credentials }),
    logout: (session) => request('/auth/logout', session, { method: 'POST' }),
    bootstrapAdmin: (details, token) => request('/auth/bootstrap-admin', null, {
      method: 'POST',
      headers: { 'X-Admin-Bootstrap-Token': token },
      body: details,
    }),
    createAdminUser: (session, details) => request('/admin/users', session, {
      method: 'POST', body: details,
    }),
    listVehicles: () => request('/vehicles'),
    listAdminVehicles: (session) => request('/admin/vehicles', session),
    createVehicle: (session, details) => request('/admin/vehicles', session, {
      method: 'POST', body: details,
    }),
    listBookings: (session) => request('/bookings', session),
    // The Node route returns customer bookings or all bookings according to the session role.
    listAdminBookings: (session) => request('/admin/bookings', session),
    listAdminMaintenance: (session) => request('/admin/maintenance', session),
    listAdminRentals: (session) => request('/admin/rentals', session),
    createBooking: (session, details) => request('/bookings', session, {
      method: 'POST', body: details,
    }),
    cancelBooking: (session, bookingId) => request(
      `/bookings/${encodeURIComponent(bookingId)}/cancel`, session, { method: 'POST' },
    ),
    listPayments: (session) => request('/payments', session),
    createPayment: (session, details) => request('/payments', session, {
      method: 'POST', body: details,
    }),
    listInvoices: (session) => request('/invoices', session),
    listAdminInvoices: (session) => request('/invoices', session),
    retryInvoiceEmail: (session, invoiceId) => request(
      `/admin/invoices/${encodeURIComponent(invoiceId)}/retry-email`, session, { method: 'POST' },
    ),
    sendChat: (session, details) => request('/chat', session, {
      method: 'POST', body: details,
    }),
  };
}
