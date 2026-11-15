const ENDPOINT = 'https://generativelanguage.googleapis.com/v1beta/models';
const DEFAULT_TIMEOUT_MS = 20_000;
const MAX_RESPONSE_BYTES = 128 * 1024;

export class GeminiClientError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function configuredCredentials(environment) {
  const apiKey = environment.GEMINI_API_KEY?.trim();
  const model = environment.GEMINI_MODEL?.trim();
  if (!apiKey || !model) {
    throw new GeminiClientError(
      503,
      'AI support is not configured. Set GEMINI_API_KEY and GEMINI_MODEL on the API server.',
    );
  }
  if (/\s/.test(apiKey) || !/^gemini-[A-Za-z0-9.-]{1,74}$/.test(model)) {
    throw new GeminiClientError(
      503,
      'AI support configuration is invalid. Check GEMINI_API_KEY and GEMINI_MODEL on the API server.',
    );
  }
  return { apiKey, model };
}

async function readResponseBody(response) {
  if (!response.body?.getReader) {
    const bytes = Buffer.from(await response.arrayBuffer());
    if (bytes.length > MAX_RESPONSE_BYTES) {
      throw new GeminiClientError(502, 'The AI support service returned an invalid response. Please try again.');
    }
    return bytes.toString('utf8');
  }

  const reader = response.body.getReader();
  const chunks = [];
  let bytesRead = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    bytesRead += value.byteLength;
    if (bytesRead > MAX_RESPONSE_BYTES) {
      await reader.cancel().catch(() => {});
      throw new GeminiClientError(502, 'The AI support service returned an invalid response. Please try again.');
    }
    chunks.push(Buffer.from(value));
  }
  return Buffer.concat(chunks, bytesRead).toString('utf8');
}

function extractAnswer(payload) {
  const parts = payload?.candidates?.[0]?.content?.parts;
  if (!Array.isArray(parts)) return '';
  return parts.map((part) => typeof part?.text === 'string' ? part.text : '').join('').trim();
}

export class GeminiClient {
  constructor({
    environment = process.env,
    fetchImpl = globalThis.fetch,
    timeoutMs = DEFAULT_TIMEOUT_MS,
  } = {}) {
    this.environment = environment;
    this.fetchImpl = fetchImpl;
    this.timeoutMs = timeoutMs;
    if (typeof fetchImpl !== 'function') throw new TypeError('Gemini client requires fetch');
    if (!Number.isInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 120_000) {
      throw new TypeError('Gemini timeout must be between 1 and 120 seconds');
    }
  }

  async generate(systemInstruction, userContent) {
    const { apiKey, model } = configuredCredentials(this.environment);
    const url = `${ENDPOINT}/${model}:generateContent`;
    const requestBody = {
      systemInstruction: { parts: [{ text: systemInstruction }] },
      contents: [{ role: 'user', parts: [{ text: userContent }] }],
      generationConfig: { maxOutputTokens: 512 },
    };
    const controller = new AbortController();
    let timeoutId;
    const timeout = new Promise((resolve, reject) => {
      timeoutId = setTimeout(() => {
        controller.abort();
        reject(new GeminiClientError(504, 'The AI support service timed out. Please try again.'));
      }, this.timeoutMs);
      timeoutId.unref?.();
    });

    const request = (async () => {
      let response;
      try {
        response = await this.fetchImpl(url, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'x-goog-api-key': apiKey,
          },
          body: JSON.stringify(requestBody),
          signal: controller.signal,
        });
      } catch {
        if (controller.signal.aborted) {
          throw new GeminiClientError(504, 'The AI support service timed out. Please try again.');
        }
        throw new GeminiClientError(503, 'AI support is temporarily unavailable. Please try again shortly.');
      }

      let responseText;
      try {
        responseText = await readResponseBody(response);
      } catch (error) {
        if (error instanceof GeminiClientError) throw error;
        if (controller.signal.aborted) {
          throw new GeminiClientError(504, 'The AI support service timed out. Please try again.');
        }
        throw new GeminiClientError(502, 'The AI support service returned an invalid response. Please try again.');
      }

      if (response.status < 200 || response.status >= 300) {
        const lowerBody = responseText.toLowerCase();
        if (response.status === 429 ||
            ((response.status === 403) && (lowerBody.includes('quota') || lowerBody.includes('resource_exhausted')))) {
          throw new GeminiClientError(
            429,
            'AI support has reached a temporary rate or free-tier quota limit. Please try again later.',
          );
        }
        if (response.status >= 500) {
          throw new GeminiClientError(503, 'The AI support service is temporarily unavailable.');
        }
        if (response.status === 401 || response.status === 403) {
          throw new GeminiClientError(
            503,
            'AI support is not configured correctly. Check the server-side Gemini key and model settings.',
          );
        }
        throw new GeminiClientError(
          502,
          'The AI support service could not process this message. Please try again.',
        );
      }

      let payload;
      try {
        payload = JSON.parse(responseText);
      } catch {
        throw new GeminiClientError(502, 'The AI support service returned no answer. Please try again.');
      }
      const answer = extractAnswer(payload);
      if (!answer) {
        throw new GeminiClientError(502, 'The AI support service returned no answer. Please try again.');
      }
      return answer;
    })();

    try {
      return await Promise.race([request, timeout]);
    } finally {
      clearTimeout(timeoutId);
    }
  }
}
