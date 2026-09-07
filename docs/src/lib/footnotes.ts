import type { Element, Root } from 'hast'
import { SKIP, visit } from 'unist-util-visit'

// remark-gfm appends the footnotes section to the end of the document, which for a page
// that wraps itself in a layout lands after the footer. Move it into a `<Footnotes />` marker.
export function footnotes() {
  return (tree: Root) => {
    const index = tree.children.findIndex((node) => node.type === 'element' && node.tagName === 'section' && 'dataFootnotes' in node.properties)
    const section = index === -1 ? undefined : (tree.children.splice(index, 1)[0] as Element)
    let placed = false

    visit(tree, 'mdxJsxFlowElement', (node, i, parent) => {
      if (node.name !== 'Footnotes' || !parent || i === undefined) return
      parent.children.splice(i, 1, ...(section ? [section] : []))
      placed = true
      return SKIP
    })

    if (section && !placed) tree.children.push(section)
  }
}
