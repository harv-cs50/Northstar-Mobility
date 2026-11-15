import { Router } from 'express';
import { ChatbotError, validateChatRequest } from '../services/chatbotService.js';

export function createChatRouter({ chatbotService, authorization, rateLimiter }) {
  const router = Router();

  router.post('/', authorization.authenticate, async (request, response, next) => {
    if (request.sessionToken && !request.principal) {
      return response.status(401).json({ error: 'The supplied session is invalid or expired' });
    }

    let chatRequest;
    try {
      chatRequest = validateChatRequest(request.body);
    } catch (error) {
      return next(error);
    }

    const principalKey = request.principal?.userId
      ? `user:${request.principal.userId}`
      : `ip:${request.ip || request.socket?.remoteAddress || 'unknown-client'}`;
    try {
      if (!(await rateLimiter.allow(principalKey))) {
        throw new ChatbotError(429, 'Chat message limit reached. Please wait a minute and try again.');
      }
      const reply = await chatbotService.reply(
        chatRequest.message,
        chatRequest.history,
        request.principal,
      );
      return response.status(200).json({ reply });
    } catch (error) {
      return next(error);
    }
  });

  return router;
}
