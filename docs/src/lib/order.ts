// sidebar and README order, by page id
export const order = [
  'quickstart',
  'overview',
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
  'packaging',
  'ci',
  'manifest',
  'config',
  'embedding',
  'workspaces',
  'why',
  'faq',
  'roadmap',
  'replace',
  'development',
]

export function position(id: string): number {
  const index = order.indexOf(id)
  if (index === -1) throw new Error(`page ${id} missing from src/lib/order.ts`)
  return index
}
