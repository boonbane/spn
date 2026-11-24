import { glob } from 'astro/loaders'
import { z } from 'astro/zod'
import { defineCollection } from 'astro:content'
import { Page } from '@spn/docs/lib/page'

const Landing = z.object({
  title: z.string(),
  description: z.string(),
})

const Pillar = z.object({
  title: z.string(),
  head: z.string(),
  code: z.string(),
  order: z.number(),
})

export const collections = {
  docs: defineCollection({ loader: glob({ pattern: '*.{md,mdx}', base: './content' }), schema: Page }),
  landing: defineCollection({ loader: glob({ pattern: '*.md', base: './content/landing' }), schema: Landing }),
  pillars: defineCollection({ loader: glob({ pattern: '*.md', base: './content/landing/pillars' }), schema: Pillar }),
}
