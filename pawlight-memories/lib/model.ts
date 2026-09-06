export type Media = { id: string; url: string; type: string; name: string };
export type Memory = {
  id: string;
  kind: 'memory' | 'anniversary' | 'companion';
  title: string;
  date: string;
  text: string;
  tags: string[];
  media: Media[];
  reminder: boolean;
  createdAt: string;
};
export type Archive = {
  name: string;
  ready: boolean;
  events: Memory[];
  avatar?: string;
};
export const feelings = ['温暖', '快乐', '平静', '想念', '难过'];
export const anniversaryKinds = ['生日', '相遇日', '离世纪念日', '自定义日期'];
export const today = () => {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
};
export function validDate(v: unknown): v is string {
  if (typeof v !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(v)) return false;
  const [y, m, d] = v.split('-').map(Number);
  return (
    y >= 1900 &&
    y <= 2100 &&
    m >= 1 &&
    m <= 12 &&
    d >= 1 &&
    d <= new Date(y, m, 0).getDate()
  );
}
export const dateLabel = (v: string) => {
  const [y, m, d] = v.split('-').map(Number);
  return `${y}年${m}月${d}日`;
};
export const blankMemory = (kind: Memory['kind'] = 'memory'): Memory => ({
  id: crypto.randomUUID(),
  kind,
  title: '',
  date: today(),
  text: '',
  tags: [],
  media: [],
  reminder: false,
  createdAt: new Date().toISOString(),
});
export function validateArchive(value: unknown): asserts value is Archive {
  const a = value as Archive;
  if (!a || typeof a.name !== 'string' || !a.name.trim())
    throw Error('请填写名字');
  if (
    typeof a.ready !== 'boolean' ||
    !Array.isArray(a.events) ||
    a.events.length > 2000 ||
    (a.avatar !== undefined &&
      a.avatar !== '' &&
      (typeof a.avatar !== 'string' || !a.avatar.startsWith('/api/media/')))
  )
    throw Error('宠物档案信息无效');
  const ids = new Set();
  for (const e of a.events) {
    if (
      !e ||
      typeof e.id !== 'string' ||
      e.id.length > 80 ||
      ids.has(e.id) ||
      !['memory', 'anniversary', 'companion'].includes(e.kind) ||
      !validDate(e.date) ||
      typeof e.title !== 'string' ||
      !e.title.trim() ||
      Array.from(e.title).length > 40 ||
      typeof e.text !== 'string' ||
      Array.from(e.text).length > 500 ||
      !Array.isArray(e.tags) ||
      e.tags.length > 5 ||
      e.tags.some((t) => !feelings.includes(t)) ||
      !Array.isArray(e.media) ||
      e.media.length > 9 ||
      typeof e.reminder !== 'boolean' ||
      typeof e.createdAt !== 'string'
    )
      throw Error('记忆内容无效，请检查日期、字数与照片数量');
    ids.add(e.id);
    for (const m of e.media)
      if (
        !m ||
        typeof m.id !== 'string' ||
        typeof m.url !== 'string' ||
        typeof m.type !== 'string' ||
        typeof m.name !== 'string' ||
        !/^(image|video)\//.test(m.type) ||
        (!m.url.startsWith('/api/media/') &&
          ![
            '/assets/portrait.png',
            '/assets/sea.png',
            '/assets/profile.png',
          ].includes(m.url))
      )
        throw Error('照片或视频信息无效');
  }
}
export function seedEvents(): Memory[] {
  return [
    {
      title: '海边散步',
      date: '2023-07-16',
      url: '/assets/sea.png',
      text: '那天阳光正好，它第一次看见海。\n它追着浪花跑了很久，回来时鼻尖沾满了细沙。',
      tags: ['快乐'],
    },
  ].map((e, i) => ({
    ...blankMemory(),
    id: `example-${i}`,
    title: e.title,
    date: e.date,
    text: e.text,
    tags: e.tags,
    media: [
      {
        id: `example-photo-${i}`,
        url: e.url,
        type: 'image/png',
        name: e.title,
      },
    ],
  }));
}
