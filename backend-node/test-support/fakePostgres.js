function normalized(sql) {
  return sql.replace(/\s+/g, ' ').trim();
}

class TransactionAdvisoryLock {
  constructor() {
    this.held = false;
    this.waiters = [];
  }

  acquire() {
    return new Promise((resolve) => {
      const grant = () => resolve(() => {
        const next = this.waiters.shift();
        if (next) next();
        else this.held = false;
      });
      if (!this.held) {
        this.held = true;
        grant();
      } else {
        this.waiters.push(grant);
      }
    });
  }
}

export class FakePostgres {
  constructor() {
    this.users = new Map();
    this.sessions = new Map();
    this.vehicles = new Map();
    this.bookings = new Map();
    this.payments = new Map();
    this.invoices = new Map();
    this.auditRows = [];
    this.clock = Date.parse('2026-01-01T00:00:00.000Z');
    this.advisoryLock = new TransactionAdvisoryLock();
    this.vehicleLocks = new Map();
    this.bookingLocks = new Map();
    this.failVehicleQueries = false;
    this.failBookingCommit = false;
    this.failPaymentInsert = false;
    this.failInvoiceInsert = false;
    this.failPaymentCommit = false;
    this.failInvoiceCommit = false;
    this.failPaymentQueries = false;
    this.failInvoiceQueries = false;
    this.failBookingPaymentLookup = false;
    this.failInvoiceClaim = false;
    this.failInvoiceStatusUpdate = false;
  }

  advanceTime(milliseconds) {
    this.clock += milliseconds;
  }

  seedUser(user) {
    this.users.set(user.user_id, { ...user });
  }

  seedVehicle(vehicle) {
    this.vehicles.set(vehicle.vehicle_id, { ...vehicle });
  }

  seedBooking(booking) {
    this.bookings.set(booking.booking_id, { ...booking });
  }

  async query(sql, values = []) {
    return this.execute(sql, values, null);
  }

  async connect() {
    return new FakePostgresClient(this);
  }

  async execute(sql, values, client) {
    const statement = normalized(sql);

    if (statement.includes('payments') && this.failPaymentQueries) {
      throw new Error('simulated payment database failure');
    }
    if (statement.includes('invoices') && this.failInvoiceQueries) {
      throw new Error('simulated invoice database failure');
    }

    if (statement === 'SELECT 1') return { rows: [{ '?column?': 1 }], rowCount: 1 };

    if (statement === 'SELECT vehicle_id FROM vehicles WHERE vehicle_id = $1 FOR UPDATE') {
      if (!client?.inTransaction) {
        throw new Error('Vehicle row lock requires a client transaction');
      }
      await client.acquireVehicleLock(values[0]);
      const vehicle = this.vehicles.get(values[0]);
      return { rows: vehicle ? [{ vehicle_id: vehicle.vehicle_id }] : [], rowCount: vehicle ? 1 : 0 };
    }

    if (statement.includes('FROM bookings b JOIN vehicles v USING (vehicle_id)')
        && statement.includes('FOR UPDATE OF b')) {
      if (this.failBookingPaymentLookup) {
        throw new Error('simulated booking lookup failure');
      }
      if (!client?.inTransaction) throw new Error('Payment lock requires a client transaction');
      const bookingId = values[0];
      await client.acquireBookingLock(bookingId);
      const booking = this.bookings.get(bookingId);
      const vehicle = booking && this.vehicles.get(booking.vehicle_id);
      if (!booking || !vehicle) return { rows: [], rowCount: 0 };
      const daysBetween = Math.floor((Date.parse(booking.end_date) - Date.parse(booking.start_date)) / 86_400_000);
      return {
        rows: [{
          customer_id: booking.customer_id,
          status: booking.status,
          days: Math.max(daysBetween, 1),
          daily_rate: vehicle.daily_rate,
        }],
        rowCount: 1,
      };
    }

    if (statement === 'SELECT customer_id FROM bookings WHERE booking_id = $1 FOR UPDATE') {
      if (!client?.inTransaction) throw new Error('Invoice lock requires a client transaction');
      await client.acquireBookingLock(values[0]);
      const booking = this.bookings.get(values[0]);
      return {
        rows: booking ? [{ customer_id: booking.customer_id }] : [],
        rowCount: booking ? 1 : 0,
      };
    }

    if (statement.includes('FROM payments') && statement.includes("payment_status = 'Successful'")) {
      const [bookingId] = values;
      const payment = this.visiblePayments(client)
        .filter((entry) => entry.booking_id === bookingId && entry.payment_status === 'Successful')
        .sort((left, right) => right.payment_id.localeCompare(left.payment_id))[0];
      return {
        rows: payment ? [{ ...payment }] : [],
        rowCount: payment ? 1 : 0,
      };
    }

    if (statement.startsWith('INSERT INTO payments ')) {
      if (!client?.inTransaction) throw new Error('Payment insert requires a client transaction');
      if (this.failPaymentInsert) throw new Error('simulated payment insert failure');
      const [paymentId, bookingId, amount, method] = values;
      if (this.visiblePayments(client).some((payment) => payment.payment_id === paymentId)) {
        const error = new Error('duplicate payment ID');
        error.code = '23505';
        throw error;
      }
      client.pendingPayments.push({
        payment_id: paymentId,
        booking_id: bookingId,
        amount: Number(amount).toFixed(2),
        payment_method: method,
        payment_status: 'Successful',
      });
      return { rows: [], rowCount: 1 };
    }

    if (statement.startsWith('INSERT INTO invoices ')) {
      if (!client?.inTransaction) throw new Error('Invoice insert requires a client transaction');
      if (this.failInvoiceInsert) throw new Error('simulated invoice insert failure');
      const [invoiceId, bookingId, subtotal, discount, tax, totalAmount] = values;
      if (this.visibleInvoices(client).some((invoice) => invoice.booking_id === bookingId)) {
        return { rows: [], rowCount: 0 };
      }
      if (this.visibleInvoices(client).some((invoice) => invoice.invoice_id === invoiceId)) {
        const error = new Error('duplicate invoice ID');
        error.code = '23505';
        throw error;
      }
      const invoice = {
        invoice_id: invoiceId,
        booking_id: bookingId,
        subtotal: Number(subtotal).toFixed(2),
        discount: Number(discount).toFixed(2),
        tax: Number(tax).toFixed(2),
        total_amount: Number(totalAmount).toFixed(2),
        email_status: 'PENDING',
        email_attempts: 0,
      };
      client.pendingInvoices.push(invoice);
      return { rows: [{ ...invoice }], rowCount: 1 };
    }

    if (statement.includes('FROM invoices i') && statement.includes('JOIN users u')
        && statement.includes('JOIN vehicles v')) {
      const invoice = this.invoices.get(values[0]);
      const booking = invoice && this.bookings.get(invoice.booking_id);
      const user = booking && this.users.get(booking.customer_id);
      const vehicle = booking && this.vehicles.get(booking.vehicle_id);
      if (!invoice || !booking || !user || !vehicle) return { rows: [], rowCount: 0 };
      const payment = this.visiblePayments(client)
        .filter((entry) => entry.booking_id === booking.booking_id)
        .sort((left, right) => right.payment_id.localeCompare(left.payment_id))[0];
      const daysBetween = Math.floor((Date.parse(booking.end_date) - Date.parse(booking.start_date)) / 86_400_000);
      return {
        rows: [{
          invoice_id: invoice.invoice_id,
          booking_id: invoice.booking_id,
          subtotal: invoice.subtotal,
          discount: invoice.discount,
          tax: invoice.tax,
          total_amount: invoice.total_amount,
          customer_id: booking.customer_id,
          customer_name: user.name,
          customer_email: user.email,
          vehicle_id: vehicle.vehicle_id,
          brand: vehicle.brand,
          model: vehicle.model,
          vehicle_type: vehicle.vehicle_type,
          daily_rate: vehicle.daily_rate,
          start_date: booking.start_date,
          end_date: booking.end_date,
          duration_days: Math.max(daysBetween, 1),
          payment_status: payment?.payment_status ?? 'Unknown',
        }],
        rowCount: 1,
      };
    }

    if (statement.startsWith("UPDATE invoices SET email_status = 'SENDING'")) {
      if (this.failInvoiceClaim) throw new Error('simulated invoice claim failure');
      const invoice = this.invoices.get(values[0]);
      if (!invoice || !['PENDING', 'FAILED'].includes(invoice.email_status)) {
        return { rows: [], rowCount: 0 };
      }
      invoice.email_status = 'SENDING';
      invoice.email_attempts += 1;
      invoice.email_last_attempt_at = new Date(this.clock).toISOString();
      invoice.email_last_error = null;
      return { rows: [{ invoice_id: invoice.invoice_id }], rowCount: 1 };
    }

    if (statement.startsWith("UPDATE invoices SET email_status = 'SENT'")) {
      if (this.failInvoiceStatusUpdate) throw new Error('simulated invoice status update failure');
      const invoice = this.invoices.get(values[0]);
      if (!invoice || invoice.email_status !== 'SENDING') return { rows: [], rowCount: 0 };
      invoice.email_status = 'SENT';
      invoice.email_sent_at = new Date(this.clock).toISOString();
      invoice.email_last_error = null;
      return { rows: [{ invoice_id: invoice.invoice_id }], rowCount: 1 };
    }

    if (statement.startsWith("UPDATE invoices SET email_status = 'FAILED'")) {
      if (this.failInvoiceStatusUpdate) throw new Error('simulated invoice status update failure');
      const [errorMessage, invoiceId] = values;
      const invoice = this.invoices.get(invoiceId);
      if (!invoice || invoice.email_status !== 'SENDING') return { rows: [], rowCount: 0 };
      invoice.email_status = 'FAILED';
      invoice.email_last_error = errorMessage;
      return { rows: [{ invoice_id: invoice.invoice_id }], rowCount: 1 };
    }

    if (statement.startsWith('SELECT email_status FROM invoices WHERE invoice_id = $1')) {
      const invoice = this.invoices.get(values[0]);
      return {
        rows: invoice ? [{ email_status: invoice.email_status }] : [],
        rowCount: invoice ? 1 : 0,
      };
    }

    if (statement.includes('FROM invoices')) {
      const customerQuery = statement.includes('WHERE b.customer_id = $1');
      const bookingQuery = statement.includes('WHERE booking_id = $1');
      const records = this.visibleInvoices(client)
        .filter((invoice) => !customerQuery
          || this.bookings.get(invoice.booking_id)?.customer_id === values[0])
        .filter((invoice) => !bookingQuery || invoice.booking_id === values[0])
        .sort((left, right) => left.invoice_id.localeCompare(right.invoice_id))
        .map((invoice) => ({ ...invoice }));
      return { rows: records, rowCount: records.length };
    }

    if (statement.includes('FROM payments')) {
      const customerQuery = statement.includes('WHERE b.customer_id = $1');
      const records = this.visiblePayments(client)
        .filter((payment) => !customerQuery
          || this.bookings.get(payment.booking_id)?.customer_id === values[0])
        .sort((left, right) => left.payment_id.localeCompare(right.payment_id))
        .map((payment) => ({ ...payment }));
      return { rows: records, rowCount: records.length };
    }

    if (statement.startsWith('SELECT booking_id FROM bookings ')) {
      const [vehicleId, requestedStart, requestedEnd, excludedBookingId] = values;
      const matching = this.visibleBookings(client).find((booking) =>
        booking.vehicle_id === vehicleId
        && booking.status !== 'CANCELLED'
        && booking.booking_id !== excludedBookingId
        && booking.start_date <= requestedEnd
        && booking.end_date >= requestedStart);
      return {
        rows: matching ? [{ booking_id: matching.booking_id }] : [],
        rowCount: matching ? 1 : 0,
      };
    }

    if (statement.startsWith('INSERT INTO bookings ')) {
      if (!client?.inTransaction) {
        throw new Error('Booking insert requires a client transaction');
      }
      const [bookingId, customerId, vehicleId, startDate, endDate] = values;
      if (this.visibleBookings(client).some((booking) => booking.booking_id === bookingId)) {
        const error = new Error('duplicate key');
        error.code = '23505';
        throw error;
      }
      const booking = {
        booking_id: bookingId,
        customer_id: customerId,
        vehicle_id: vehicleId,
        start_date: startDate,
        end_date: endDate,
        status: 'CONFIRMED',
      };
      client.pendingBookings.push(booking);
      return { rows: [], rowCount: 1 };
    }

    if (statement.includes('FROM bookings')) {
      const customerQuery = statement.includes('WHERE customer_id = $1');
      const records = this.visibleBookings(client)
        .filter((booking) => !customerQuery || booking.customer_id === values[0])
        .sort((left, right) => {
          if (statement.includes('ORDER BY start_date DESC')) {
            return right.start_date.localeCompare(left.start_date)
              || left.booking_id.localeCompare(right.booking_id);
          }
          return left.start_date.localeCompare(right.start_date);
        })
        .map((booking) => ({ ...booking }));
      return { rows: records, rowCount: records.length };
    }

    if (statement.startsWith("UPDATE bookings SET status = 'CANCELLED'")) {
      const [bookingId, customerId] = values;
      const booking = this.bookings.get(bookingId);
      const ownerConstraint = statement.includes('AND customer_id = $2');
      if (!booking || (ownerConstraint && booking.customer_id !== customerId)) {
        return { rows: [], rowCount: 0 };
      }
      booking.status = 'CANCELLED';
      return {
        rows: [{ booking_id: booking.booking_id, customer_id: booking.customer_id }],
        rowCount: 1,
      };
    }

    if (statement.includes('FROM vehicles')) {
      if (this.failVehicleQueries) {
        throw new Error('private vehicle database error detail');
      }
      const records = [...this.vehicles.values()]
        .filter((vehicle) => !statement.includes('WHERE is_available = TRUE') || vehicle.is_available)
        .sort((left, right) => left.vehicle_id.localeCompare(right.vehicle_id));
      return { rows: records.map((vehicle) => ({ ...vehicle })), rowCount: records.length };
    }

    if (statement.startsWith('INSERT INTO vehicles ')) {
      if (this.failVehicleQueries) {
        throw new Error('private vehicle database error detail');
      }
      const [id, brand, model, type, dailyRate] = values;
      if (this.vehicles.has(id)) {
        const error = new Error('duplicate key');
        error.code = '23505';
        throw error;
      }
      this.vehicles.set(id, {
        vehicle_id: id,
        brand,
        model,
        vehicle_type: type,
        daily_rate: String(dailyRate),
        is_available: true,
      });
      return { rows: [], rowCount: 1 };
    }

    if (statement === 'BEGIN') {
      client.begin();
      return { rows: [], rowCount: 0 };
    }
    if (statement === 'COMMIT') {
      if (client?.pendingBookings.length > 0 && this.failBookingCommit) {
        throw new Error('simulated booking commit failure');
      }
      if (client?.pendingPayments.length > 0 && this.failPaymentCommit) {
        throw new Error('simulated payment commit failure');
      }
      if (client?.pendingInvoices.length > 0 && this.failInvoiceCommit) {
        throw new Error('simulated invoice commit failure');
      }
      client.commit();
      return { rows: [], rowCount: 0 };
    }
    if (statement === 'ROLLBACK') {
      client.rollback();
      return { rows: [], rowCount: 0 };
    }
    if (statement === 'SELECT pg_advisory_xact_lock(1448230227, 1229863252)') {
      if (!client) throw new Error('Advisory transaction lock requires a client transaction');
      await client.acquireAdvisoryLock();
      return { rows: [{ pg_advisory_xact_lock: null }], rowCount: 1 };
    }

    if (statement.includes("SELECT user_id FROM users WHERE role = 'ADMIN' LIMIT 1")) {
      const allUsers = this.visibleUsers(client);
      const admins = [...allUsers.values()].filter((user) => user.role === 'ADMIN');
      return { rows: admins.slice(0, 1).map((user) => ({ user_id: user.user_id })), rowCount: Math.min(admins.length, 1) };
    }

    if (statement.includes('SELECT user_id FROM users WHERE user_id = $1 OR email = $2')) {
      const [userId, email] = values;
      const user = [...this.visibleUsers(client).values()]
        .find((entry) => entry.user_id === userId || entry.email === email);
      return { rows: user ? [{ user_id: user.user_id }] : [], rowCount: user ? 1 : 0 };
    }

    if (statement.includes('SELECT user_id, role FROM users WHERE user_id = $1 FOR SHARE')) {
      const user = this.visibleUsers(client).get(values[0]);
      return { rows: user ? [{ user_id: user.user_id, role: user.role }] : [], rowCount: user ? 1 : 0 };
    }

    if (statement.includes('SELECT user_id, name, email, password_hash, role FROM users WHERE email = $1')) {
      const user = [...this.users.values()].find((entry) => entry.email === values[0]);
      return { rows: user ? [{ ...user }] : [], rowCount: user ? 1 : 0 };
    }

    if (statement.startsWith('INSERT INTO users ')) {
      const [userId, name, email, passwordHash] = values;
      const role = statement.includes("'ADMIN'") ? 'ADMIN' : 'CUSTOMER';
      const conflicting = [...this.visibleUsers(client).values()]
        .some((entry) => entry.user_id === userId || entry.email === email);
      if (conflicting) {
        const error = new Error('duplicate key');
        error.code = '23505';
        throw error;
      }
      const user = {
        user_id: userId,
        name,
        email,
        password_hash: passwordHash,
        role,
        membership_level: 'Regular',
      };
      if (client) client.pendingUsers.push(user);
      else this.users.set(userId, user);
      return { rows: [{ user_id: userId, name, email, role }], rowCount: 1 };
    }

    if (statement.startsWith('INSERT INTO auth_sessions ')) {
      const [tokenHash, userId, role] = values;
      const session = {
        session_token_hash: tokenHash,
        user_id: userId,
        role,
        created_at: this.clock,
        last_activity_at: this.clock,
        expires_at: this.clock + 30 * 60 * 1000,
      };
      if (client) client.pendingSessions.push(session);
      else this.sessions.set(tokenHash, session);
      return { rows: [], rowCount: 1 };
    }

    if (statement.startsWith('UPDATE auth_sessions AS s ')) {
      const [tokenHash] = values;
      const session = this.sessions.get(tokenHash);
      const user = session && this.users.get(session.user_id);
      if (!session || session.expires_at <= this.clock || user?.role !== session.role) {
        return { rows: [], rowCount: 0 };
      }
      session.last_activity_at = this.clock;
      session.expires_at = this.clock + 30 * 60 * 1000;
      return { rows: [{ user_id: session.user_id, role: session.role }], rowCount: 1 };
    }

    if (statement.startsWith('DELETE FROM auth_sessions WHERE session_token_hash = $1')) {
      const deleted = this.sessions.delete(values[0]);
      return { rows: deleted ? [{ user_id: 'deleted' }] : [], rowCount: deleted ? 1 : 0 };
    }

    if (statement.startsWith('INSERT INTO audit_logs ')) {
      const [logId, userId, action, description] = values;
      this.auditRows.push({ logId, userId, action, description });
      return { rows: [], rowCount: 1 };
    }

    throw new Error(`Unexpected test SQL: ${statement}`);
  }

  visibleUsers(client) {
    const users = new Map(this.users);
    for (const user of client?.pendingUsers ?? []) users.set(user.user_id, user);
    return users;
  }

  visibleBookings(client) {
    const bookings = new Map(this.bookings);
    for (const booking of client?.pendingBookings ?? []) bookings.set(booking.booking_id, booking);
    return [...bookings.values()];
  }

  visiblePayments(client) {
    const payments = new Map(this.payments);
    for (const payment of client?.pendingPayments ?? []) payments.set(payment.payment_id, payment);
    return [...payments.values()];
  }

  visibleInvoices(client) {
    const invoices = new Map(this.invoices);
    for (const invoice of client?.pendingInvoices ?? []) invoices.set(invoice.invoice_id, invoice);
    return [...invoices.values()];
  }
}

class FakePostgresClient {
  constructor(database) {
    this.database = database;
    this.pendingUsers = [];
    this.pendingSessions = [];
    this.pendingBookings = [];
    this.pendingPayments = [];
    this.pendingInvoices = [];
    this.releaseLock = null;
    this.releaseVehicleLocks = new Map();
    this.releaseBookingLocks = new Map();
    this.inTransaction = false;
    this.finished = false;
  }

  begin() {
    if (this.finished) throw new Error('Transaction already finished');
    this.inTransaction = true;
  }

  async acquireAdvisoryLock() {
    if (!this.releaseLock) this.releaseLock = await this.database.advisoryLock.acquire();
  }

  async acquireVehicleLock(vehicleId) {
    if (!this.releaseVehicleLocks.has(vehicleId)) {
      const lock = this.database.vehicleLocks.get(vehicleId)
        ?? new TransactionAdvisoryLock();
      this.database.vehicleLocks.set(vehicleId, lock);
      this.releaseVehicleLocks.set(vehicleId, await lock.acquire());
    }
  }

  async acquireBookingLock(bookingId) {
    if (!this.releaseBookingLocks.has(bookingId)) {
      const lock = this.database.bookingLocks.get(bookingId)
        ?? new TransactionAdvisoryLock();
      this.database.bookingLocks.set(bookingId, lock);
      this.releaseBookingLocks.set(bookingId, await lock.acquire());
    }
  }

  async query(sql, values = []) {
    return this.database.execute(sql, values, this);
  }

  commit() {
    for (const user of this.pendingUsers) this.database.users.set(user.user_id, user);
    for (const session of this.pendingSessions) {
      this.database.sessions.set(session.session_token_hash, session);
    }
    for (const booking of this.pendingBookings) {
      this.database.bookings.set(booking.booking_id, booking);
    }
    for (const payment of this.pendingPayments) {
      this.database.payments.set(payment.payment_id, payment);
    }
    for (const invoice of this.pendingInvoices) {
      this.database.invoices.set(invoice.invoice_id, invoice);
    }
    this.finish();
  }

  rollback() {
    this.pendingUsers = [];
    this.pendingSessions = [];
    this.pendingBookings = [];
    this.pendingPayments = [];
    this.pendingInvoices = [];
    this.finish();
  }

  finish() {
    this.finished = true;
    this.releaseLock?.();
    this.releaseLock = null;
    for (const release of this.releaseVehicleLocks.values()) release();
    this.releaseVehicleLocks.clear();
    for (const release of this.releaseBookingLocks.values()) release();
    this.releaseBookingLocks.clear();
    this.inTransaction = false;
  }

  release() {
    if (!this.finished) this.rollback();
  }
}
