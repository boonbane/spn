export interface Target {
  id: string
  label: string
  lang: string
  prompt: string
  command: string
}

const releases = 'https://github.com/boonbane/spn/releases/latest/download'

export const targets: Target[] = [
  { id: 'posix', label: 'macOS & Linux', lang: 'sh', prompt: '$', command: `curl -fsSL ${releases}/install.sh | sh` },
  { id: 'windows', label: 'Windows', lang: 'powershell', prompt: '>', command: `irm ${releases}/install.ps1 | iex` },
]
