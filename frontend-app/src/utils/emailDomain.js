const ALLOWED_EMAIL_DOMAINS = ['mail.ru', 'g.nsu.ru', 'gmail.com', 'yandex.ru']

export function isAllowedEmailDomain(email) {
  if (!email || !email.includes('@')) return false
  const domain = email.split('@').pop().trim().toLowerCase()
  return ALLOWED_EMAIL_DOMAINS.includes(domain)
}

export function getEmailDomainError() {
  return 'Почта должна оканчиваться на @mail.ru, @g.nsu.ru, @gmail.com или @yandex.ru'
}
