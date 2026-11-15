import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { ApiError, createVrmsApi } from './api.js';

function mockFetch(response = { ok: true, status: 200, json: async () => ({ ok: true }) }) {
  const calls = [];
  const fetch = async (url, options) => {
    calls.push({ url, options });
    return response;
  };
  return { calls, fetch };
}

describe('Northstar frontend API integration', () => {
  it('routes every frontend workflow to the Node API with the expected method and session', async () => {
    const { calls, fetch } = mockFetch();
    const api = createVrmsApi('https://api.northstar.example/api/', fetch);
    const session = { sessionId: 'opaque-session-id', userId: 'CUST-1', role: 'CUSTOMER' };
    const admin = { sessionId: 'opaque-admin-session', userId: 'ADMIN-1', role: 'ADMIN' };

    await api.registerCustomer({ userId: 'CUST-1', name: 'Customer', email: 'c@example.test', password: 'example' });
    await api.login({ email: 'c@example.test', password: 'example' });
    await api.logout(session);
    await api.bootstrapAdmin({ userId: 'ADMIN-1', name: 'Admin' }, 'bootstrap-token');
    await api.createAdminUser(admin, { userId: 'ADMIN-2', name: 'Other admin' });
    await api.listVehicles();
    await api.listAdminVehicles(admin);
    await api.createVehicle(admin, { id: 'V-1', brand: 'Make', model: 'Model', type: 'Car', dailyRate: 100 });
    await api.listBookings(session);
    await api.listAdminBookings(admin);
    await api.listAdminMaintenance(admin);
    await api.listAdminRentals(admin);
    await api.createBooking(session, { vehicleId: 'V-1', startDate: '2026-12-01', endDate: '2026-12-02' });
    await api.cancelBooking(session, 'BOOK/1');
    await api.listPayments(session);
    await api.createPayment(session, { bookingId: 'BOOK-1', method: 'UPI' });
    await api.listInvoices(session);
    await api.listAdminInvoices(admin);
    await api.retryInvoiceEmail(admin, 'INV/1');
    await api.sendChat(session, { message: 'Which vehicles are available?', history: '' });

    const expected = [
      ['POST', '/api/auth/register'], ['POST', '/api/auth/login'], ['POST', '/api/auth/logout'],
      ['POST', '/api/auth/bootstrap-admin'], ['POST', '/api/admin/users'],
      ['GET', '/api/vehicles'], ['GET', '/api/admin/vehicles'], ['POST', '/api/admin/vehicles'],
      ['GET', '/api/bookings'], ['GET', '/api/admin/bookings'],
      ['GET', '/api/admin/maintenance'], ['GET', '/api/admin/rentals'], ['POST', '/api/bookings'],
      ['POST', '/api/bookings/BOOK%2F1/cancel'], ['GET', '/api/payments'], ['POST', '/api/payments'],
      ['GET', '/api/invoices'], ['GET', '/api/invoices'],
      ['POST', '/api/admin/invoices/INV%2F1/retry-email'], ['POST', '/api/chat'],
    ];

    assert.equal(calls.length, expected.length);
    calls.forEach(({ url, options }, index) => {
      const parsed = new URL(url);
      assert.equal(parsed.origin, 'https://api.northstar.example');
      assert.equal(parsed.pathname, expected[index][1]);
      assert.equal(options.method ?? 'GET', expected[index][0]);
    });
    assert.equal(calls[0].options.headers.Authorization, undefined);
    assert.equal(calls[5].options.headers.Authorization, undefined);
    assert.equal(calls[3].options.headers['X-Admin-Bootstrap-Token'], 'bootstrap-token');
    for (const index of [2, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]) {
      assert.match(calls[index].options.headers.Authorization, /^Bearer /);
    }
    assert.deepEqual(JSON.parse(calls[15].options.body), { bookingId: 'BOOK-1', method: 'UPI' });
  });

  it('uses the same-origin /api base by default and removes trailing base slashes', async () => {
    const local = mockFetch();
    await createVrmsApi(undefined, local.fetch).listVehicles();
    assert.equal(local.calls[0].url, '/api/vehicles');

    const configured = mockFetch();
    await createVrmsApi('https://api.example.test/api///', configured.fetch).listVehicles();
    assert.equal(configured.calls[0].url, 'https://api.example.test/api/vehicles');
  });

  it('preserves backend JSON errors and HTTP status for frontend feedback', async () => {
    const { fetch } = mockFetch({
      ok: false,
      status: 409,
      json: async () => ({ error: 'Vehicle is already booked for the requested dates' }),
    });
    await assert.rejects(createVrmsApi('/api', fetch).createBooking(
      { sessionId: 'opaque-session' },
      { vehicleId: 'V-1', startDate: '2026-12-01', endDate: '2026-12-02' },
    ), (error) => error instanceof ApiError && error.status === 409 &&
      error.message === 'Vehicle is already booked for the requested dates');
  });

  it('returns a safe message for network failures', async () => {
    const api = createVrmsApi('/api', async () => { throw new Error('secret transport details'); });
    await assert.rejects(api.listVehicles(), (error) => error instanceof ApiError &&
      error.status === 0 && !error.message.includes('secret transport details'));
  });

  it('signals the app to clear a persisted session after an authenticated 401', async () => {
    const previousWindow = globalThis.window;
    const events = [];
    globalThis.window = { dispatchEvent: (event) => events.push(event.type) };
    try {
      const { fetch } = mockFetch({ ok: false, status: 401, json: async () => ({ error: 'Authentication required' }) });
      await assert.rejects(createVrmsApi('/api', fetch).listBookings({ sessionId: 'expired-session' }));
      assert.deepEqual(events, ['vrms:session-expired']);
    } finally {
      if (previousWindow === undefined) delete globalThis.window;
      else globalThis.window = previousWindow;
    }
  });
});
