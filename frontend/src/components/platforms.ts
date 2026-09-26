/** XDLINX satellite platforms, as published on xdlinx.space/platform. */
export interface Platform {
  id: string
  name: string
  image: string
  url: string
  bus: string
  payload: string
  power: string
  solar: string
  heritage: string
}

export const PLATFORMS: Platform[] = [
  { id: 'ns', name: 'XDSAT NS', image: '/xdlinx/NS.webp', url: 'https://xdlinx.space/platform/#ns',
    bus: '6U', payload: '4U', power: '126 Wh', solar: '66 W', heritage: 'Launched 2023' },
  { id: 'nl', name: 'XDSAT NL', image: '/xdlinx/NL.webp', url: 'https://xdlinx.space/platform/#nl',
    bus: '12U – 16U', payload: '8U', power: '210 Wh', solar: '105 W', heritage: 'Q2 2024' },
  { id: 'm200', name: 'XDSAT M200', image: '/xdlinx/M200.webp', url: 'https://xdlinx.space/platform/#m200',
    bus: 'M200', payload: '25 – 40 kg', power: '350 Wh', solar: '200 W', heritage: 'Q4 2024' },
  { id: 'm400', name: 'XDSAT M400', image: '/xdlinx/M400.webp', url: 'https://xdlinx.space/platform/#m400',
    bus: 'M400', payload: '40 – 75 kg', power: '700 Wh', solar: '400 W', heritage: 'Q1 2025' },
  { id: 'm600', name: 'XDSAT M600', image: '/xdlinx/M600.webp', url: 'https://xdlinx.space/platform/#m600',
    bus: 'M600', payload: '75 – 100 kg', power: '1,100 Wh', solar: '600 W', heritage: 'Q4 2024' },
]

export const XDLINX_LINKS: { label: string; url: string }[] = [
  { label: 'xdlinx.space', url: 'https://xdlinx.space/' },
  { label: 'Platforms', url: 'https://xdlinx.space/platform/' },
  { label: 'Team', url: 'https://xdlinx.space/xdlinx-space-labs-team/' },
  { label: 'News & press', url: 'https://xdlinx.space/xdlinx-space-labs-news/' },
  { label: 'Blog', url: 'https://xdlinx.space/blog/' },
  { label: 'Careers', url: 'https://xdlinx.space/careers/' },
  { label: 'Contact', url: 'https://xdlinx.space/xdlinx-space-labs-contact-us/' },
  { label: 'LinkedIn', url: 'http://www.linkedin.com/company/xdlinx-labs' },
  { label: 'Instagram', url: 'https://www.instagram.com/xdlinxlab/' },
]
