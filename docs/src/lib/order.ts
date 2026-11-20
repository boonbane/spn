// sidebar and README order, by page id
export const order = [
  'quickstart',
  'overview',
  'packages',
  'dependencies',
  'building',
  'metaprograms',
  'task',
  'toolchains',
  'indexes',
  'cache',
  'configuration',
  'manifest',
  'config',
  'embedding',
  'workspaces',
  'why',
  'faq',
  'replace',
  'development',
]

export function position(id: string): number {
  const index = order.indexOf(id)
  if (index === -1) throw new Error(`page ${id} missing from src/lib/order.ts`)
  return index
}
