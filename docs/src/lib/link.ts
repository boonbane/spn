import type { Root } from 'mdast'
import { visit } from 'unist-util-visit'
// loaded by astro.config.ts, where the @spn/docs alias doesn't resolve
import links from '../../content/links.json'

const SCHEME = 'link:'

export function named(url: string): boolean {
  return url.startsWith(SCHEME)
}

// `link:discord.public` names an entry of content/links.json by its path
export function resolve(url: string): string {
  const path = url.slice(SCHEME.length).split('.')
  const found = path.reduce<unknown>((node, key) => (node as Record<string, unknown> | undefined)?.[key], links)
  if (typeof found !== 'string') throw new Error(`unknown link ${url}`)
  return found
}

export function linkScheme() {
  return (tree: Root) => {
    visit(tree, 'link', (node) => {
      if (named(node.url)) node.url = resolve(node.url)
    })
  }
}
