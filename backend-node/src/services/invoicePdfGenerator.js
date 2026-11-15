import PDFDocument from 'pdfkit';

function safeText(value) {
  return String(value ?? '').replace(/[\r\n\t]/g, ' ').replace(/[^\x20-\x7E]/g, '?');
}

function money(value) {
  return `INR ${Number(value).toFixed(2)}`;
}

function validateDocument(invoice) {
  const requiredText = [
    invoice?.invoiceId,
    invoice?.bookingId,
    invoice?.customerId,
    invoice?.customerName,
    invoice?.customerEmail,
    invoice?.vehicleId,
    invoice?.vehicleBrand,
    invoice?.vehicleModel,
    invoice?.vehicleType,
    invoice?.rentalStartDate,
    invoice?.rentalEndDate,
    invoice?.paymentStatus,
  ];
  if (requiredText.some((value) => typeof value !== 'string' || !value.trim())) {
    throw new TypeError('Invoice document contains invalid required data');
  }
  if (!Number.isInteger(invoice.durationDays) || invoice.durationDays < 1
      || !Number.isFinite(invoice.dailyRate) || invoice.dailyRate <= 0
      || !Number.isFinite(invoice.subtotal) || invoice.subtotal <= 0
      || !Number.isFinite(invoice.discount) || invoice.discount < 0
      || !Number.isFinite(invoice.tax) || invoice.tax < 0
      || !Number.isFinite(invoice.total) || invoice.total <= 0) {
    throw new TypeError('Invoice document contains invalid required data');
  }
}

export function generateInvoicePdf(invoice) {
  validateDocument(invoice);

  return new Promise((resolve, reject) => {
    const document = new PDFDocument({
      size: 'A4',
      margins: { top: 48, right: 48, bottom: 48, left: 48 },
      compress: false,
      info: {
        Title: 'Vehicle Rental Invoice',
        Author: 'Northstar Mobility',
      },
    });
    const chunks = [];
    document.on('data', (chunk) => chunks.push(chunk));
    document.once('error', reject);
    document.once('end', () => resolve(Buffer.concat(chunks)));

    try {
      const left = 48;
      const right = 547;
      const width = 595;
      const green = '#173f32';
      const muted = '#617368';
      const line = '#d7ded7';
      const cream = '#f4f5ed';
      const text = (value) => safeText(value);

      document.rect(0, 0, width, 124).fill(green);
      document.fillColor(cream).font('Helvetica-Bold').fontSize(19)
        .text('NORTHSTAR MOBILITY', left, 35);
      document.fillColor('#b8cbbd').font('Helvetica').fontSize(9)
        .text('VEHICLE RENTAL MANAGEMENT', left, 61);
      document.fillColor(cream).font('Helvetica-Bold').fontSize(25)
        .text('INVOICE', 350, 36, { width: right - 350, align: 'right' });
      document.fillColor('#b8cbbd').font('Helvetica').fontSize(9)
        .text(`Invoice ${text(invoice.invoiceId)}`, 330, 68, { width: right - 330, align: 'right' });

      document.fillColor(green).font('Helvetica-Bold').fontSize(9).text('BILLED TO', left, 154);
      document.font('Helvetica-Bold').fontSize(12).text(text(invoice.customerName), left, 174, { width: 250 });
      document.fillColor(muted).font('Helvetica').fontSize(9)
        .text(text(invoice.customerEmail), left, 194, { width: 250 });
      document.text(`Customer ID: ${text(invoice.customerId)}`, left, 211, { width: 250 });

      document.fillColor(green).font('Helvetica-Bold').fontSize(9)
        .text('BOOKING REFERENCE', 350, 154);
      document.font('Helvetica').fontSize(10).text(text(invoice.bookingId), 350, 174);
      document.fillColor(muted).fontSize(9)
        .text(`Payment status: ${text(invoice.paymentStatus)}`, 350, 194);

      document.moveTo(left, 238).lineTo(right, 238).strokeColor(line).lineWidth(0.8).stroke();
      document.fillColor(green).font('Helvetica-Bold').fontSize(9)
        .text('VEHICLE & RENTAL DETAILS', left, 256);
      document.font('Helvetica-Bold').fontSize(11)
        .text(`${text(invoice.vehicleBrand)} ${text(invoice.vehicleModel)} (${text(invoice.vehicleType)})`, left, 278);
      document.fillColor(muted).font('Helvetica').fontSize(9)
        .text(`Vehicle ID: ${text(invoice.vehicleId)}`, left, 298);
      document.text(`Rental dates: ${text(invoice.rentalStartDate)} to ${text(invoice.rentalEndDate)}`, left, 316);
      document.fillColor(green).font('Helvetica-Bold').fontSize(10)
        .text(`${invoice.durationDays} ${invoice.durationDays === 1 ? 'day' : 'days'}`, 430, 278,
          { width: right - 430, align: 'right' });

      document.moveTo(left, 350).lineTo(right, 350).strokeColor(line).stroke();
      document.fillColor(muted).font('Helvetica-Bold').fontSize(8.5)
        .text('DESCRIPTION', left, 366);
      document.text('AMOUNT', 430, 366, { width: right - 430, align: 'right' });
      document.moveTo(left, 382).lineTo(right, 382).strokeColor(line).stroke();

      document.fillColor(green).font('Helvetica').fontSize(10)
        .text(`Vehicle rental - ${text(invoice.vehicleBrand)} ${text(invoice.vehicleModel)}`, left, 400);
      document.fillColor(muted).fontSize(8.5)
        .text(`${invoice.durationDays} days x ${money(invoice.dailyRate)} / day`, left, 418);
      document.fillColor(green).font('Helvetica').fontSize(10)
        .text(money(invoice.subtotal), 390, 400, { width: right - 390, align: 'right' });
      document.moveTo(left, 441).lineTo(right, 441).strokeColor(line).stroke();

      const rows = [
        ['Subtotal', invoice.subtotal],
        ['Discount', -invoice.discount],
        ['Taxes', invoice.tax],
      ];
      let rowY = 460;
      for (const [label, amount] of rows) {
        document.fillColor(muted).font('Helvetica').fontSize(9.5).text(label, 350, rowY);
        const displayAmount = label === 'Discount' ? `- ${money(invoice.discount)}` : money(amount);
        document.fillColor(green).text(displayAmount, 390, rowY, { width: right - 390, align: 'right' });
        rowY += 21;
      }
      document.moveTo(345, rowY + 2).lineTo(right, rowY + 2).strokeColor(line).stroke();
      document.fillColor(green).font('Helvetica-Bold').fontSize(12).text('TOTAL PAID', 350, rowY + 15);
      document.fontSize(13).text(money(invoice.total), 390, rowY + 14,
        { width: right - 390, align: 'right' });

      document.moveTo(left, 752).lineTo(right, 752).strokeColor(line).stroke();
      document.fillColor(green).font('Helvetica-Bold').fontSize(8.5).text('PAYMENT', left, 768);
      document.fillColor(muted).font('Helvetica').fontSize(8.5)
        .text(`Payment status: ${text(invoice.paymentStatus)} | This project uses simulated payments; no real charge was made.`,
          left, 784, { width: right - left });
      document.text('Thank you for choosing Northstar Mobility.', left, 811,
        { width: right - left, align: 'right' });

      document.end();
    } catch (error) {
      document.destroy(error);
      reject(error);
    }
  });
}
