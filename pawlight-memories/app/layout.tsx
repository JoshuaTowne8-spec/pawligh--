import type { Metadata } from 'next';
import './globals.css';
export const metadata: Metadata={title:'PawLight · 宠物纪念空间',description:'珍藏每段回忆，记住重要的日子，让陪伴继续。'};
export default function RootLayout({children}:{children:React.ReactNode}){return <html lang="zh-CN"><head><meta name="theme-color" content="#fff9ea"/><meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover"/></head><body>{children}</body></html>}
