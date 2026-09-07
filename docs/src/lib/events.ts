export function send(event: string, detail = '') {
  const referrer = document.referrer ? new URL(document.referrer).host : ''
  navigator.sendBeacon('/e', JSON.stringify({ event, detail, path: location.pathname, referrer }))
}
