// sidebar and README order, by page id
export const order = [
  'quickstart',
  'packages',
  'conditions',
  'dependencies',
  'indexes',
  'building',
  'metaprograms',
  'task',
  'toolchains',
  'cache',
  'configuration',
  'ci',
  'manifest',
  'config',
  'embedding',
  'faq',
  'roadmap',
]

export function position(id: string): number {
  const index = order.indexOf(id)
  if (index === -1) throw new Error(`page ${id} missing from src/lib/order.ts`)
  return index
}
