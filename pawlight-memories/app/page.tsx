'use client';
import { useEffect, useRef, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Switch } from '@/components/ui/switch';
import {
  Dialog,
  DialogContent,
  DialogTitle,
  DialogDescription,
} from '@/components/ui/dialog';
import {
  type Archive,
  type Memory,
  type Media,
  blankMemory,
  today,
  dateLabel,
  feelings,
  anniversaryKinds,
  seedEvents,
  validDate,
} from '@/lib/model';

const A = '/assets/';
type DeviceState = {
  connected: boolean;
  always_on: boolean;
  audio_enabled: boolean;
};
type DeviceEvent = {
  id: string;
  type: 'memory_recalled' | 'companion_triggered';
  trigger?: 'touch' | 'voice';
  touch_kind?: 'quick' | 'slow';
  memory_id?: string;
  memory_title?: string;
  emotion: string;
  transcript: string;
  created_at: string;
};
const emotionTag: Record<string, string> = {
  warm: '温暖',
  happy: '快乐',
  calm: '平静',
  miss: '想念',
  sad: '难过',
};
function Icon({ name, size = 28 }: { name: string; size?: number }) {
  return (
    <img
      className="icon"
      src={`${A}${name}.svg`}
      style={{ width: size, height: size }}
      alt=""
    />
  );
}
function Action({
  children,
  onClick,
  disabled = false,
  secondary = false,
}: {
  children: React.ReactNode;
  onClick: () => void;
  disabled?: boolean;
  secondary?: boolean;
}) {
  return (
    <Button
      className={secondary ? 'secondary' : 'primary'}
      disabled={disabled}
      onClick={onClick}
    >
      {!secondary && <Icon name="paw" />}
      {children}
    </Button>
  );
}
function MediaView({
  media,
  large = false,
}: {
  media: Media;
  large?: boolean;
}) {
  const [failed, setFailed] = useState(false);
  return failed ? (
    <div className="media-fallback">
      此格式暂无法预览
      <a href={media.url} download={media.name}>
        查看原文件
      </a>
    </div>
  ) : media.type.startsWith('video/') ? (
    <video
      className={large ? 'large-media' : ''}
      src={media.url}
      controls
      playsInline
      preload="metadata"
      onError={() => setFailed(true)}
    />
  ) : (
    <img
      className={large ? 'large-media' : ''}
      src={media.url}
      alt={media.name}
      onError={() => setFailed(true)}
    />
  );
}
function Gallery({
  media,
  remove,
}: {
  media: Media[];
  remove?: (i: number) => void;
}) {
  return (
    <div className={`gallery ${media.length === 1 ? 'single' : ''}`}>
      {media.map((m, i) => (
        <div className="photo" key={m.id}>
          <MediaView media={m} />
          {remove && (
            <Button
              className="remove"
              aria-label={`移除第 ${i + 1} 个文件`}
              onClick={() => remove(i)}
            >
              ×
            </Button>
          )}
        </div>
      ))}
    </div>
  );
}
function Calendar({
  value,
  onChange,
}: {
  value: string;
  onChange: (v: string) => void;
}) {
  const [view, setView] = useState(value || today());
  const [y, m] = view.split('-').map(Number);
  const count = new Date(y, m, 0).getDate(),
    offset = (new Date(y, m - 1, 1).getDay() + 6) % 7;
  function shift(n: number) {
    const d = new Date(y, m - 1 + n, 1);
    if (d.getFullYear() >= 1900 && d.getFullYear() <= 2100)
      setView(
        `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-01`,
      );
  }
  return (
    <section className="calendar">
      <div className="calendar-head">
        <Button
          aria-label="上个月"
          className="plain"
          disabled={y === 1900 && m === 1}
          onClick={() => shift(-1)}
        >
          ‹
        </Button>
        <select
          aria-label="年份"
          value={y}
          onChange={(e) =>
            setView(`${e.target.value}-${String(m).padStart(2, '0')}-01`)
          }
        >
          {Array.from({ length: 201 }, (_, i) => 1900 + i).map((year) => (
            <option key={year}>{year}</option>
          ))}
        </select>
        <select
          aria-label="月份"
          value={m}
          onChange={(e) =>
            setView(`${y}-${e.target.value.padStart(2, '0')}-01`)
          }
        >
          {Array.from({ length: 12 }, (_, i) => (
            <option value={i + 1} key={i}>
              {i + 1}月
            </option>
          ))}
        </select>
        <Button
          aria-label="下个月"
          className="plain"
          disabled={y === 2100 && m === 12}
          onClick={() => shift(1)}
        >
          ›
        </Button>
      </div>
      <div className="days">
        {['一', '二', '三', '四', '五', '六', '日'].map((d) => (
          <span className="weekday" key={d}>
            {d}
          </span>
        ))}
        {Array.from({ length: offset }, (_, i) => (
          <span key={`empty${i}`} />
        ))}
        {Array.from({ length: count }, (_, i) => {
          const date = `${y}-${String(m).padStart(2, '0')}-${String(i + 1).padStart(2, '0')}`;
          return (
            <Button
              key={date}
              className={`day ${date === value ? 'selected' : ''}`}
              aria-label={dateLabel(date)}
              aria-pressed={date === value}
              onClick={() => onChange(date)}
            >
              {i + 1}
            </Button>
          );
        })}
      </div>
    </section>
  );
}
function downloadReminder(e: Memory) {
  const [y, m, d] = e.date.split('-').map(Number),
    now = new Date();
  let year = Math.max(y, now.getFullYear());
  if (new Date(year, m - 1, d, 9) < now) year++;
  while (new Date(year, m - 1, d).getMonth() !== m - 1) year++;
  const stamp = `${year}${String(m).padStart(2, '0')}${String(d).padStart(2, '0')}T090000`;
  const escape = (s: string) =>
    s
      .replaceAll('\\', '\\\\')
      .replaceAll('\n', '\\n')
      .replaceAll(',', '\\,')
      .replaceAll(';', '\\;');
  const content = [
    'BEGIN:VCALENDAR',
    'VERSION:2.0',
    'PRODID:-//PawLight//Memory//ZH',
    'BEGIN:VEVENT',
    `UID:${e.id}@pawlight`,
    `DTSTAMP:${new Date().toISOString().replace(/[-:]/g, '').split('.')[0]}Z`,
    `DTSTART;TZID=Asia/Shanghai:${stamp}`,
    `SUMMARY:${escape(e.title)}`,
    'RRULE:FREQ=YEARLY',
    'BEGIN:VALARM',
    'TRIGGER:PT0M',
    'ACTION:DISPLAY',
    `DESCRIPTION:${escape(e.title)}`,
    'END:VALARM',
    'END:VEVENT',
    'END:VCALENDAR',
  ].join('\r\n');
  const url = URL.createObjectURL(
    new Blob([content], { type: 'text/calendar;charset=utf-8' }),
  );
  const a = document.createElement('a');
  a.href = url;
  a.download = `${e.title}.ics`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export default function Home() {
  const [screen, setScreen] = useState(1),
    [archive, setArchive] = useState<Archive>({
      name: '',
      ready: false,
      events: [],
    }),
    [version, setVersion] = useState(0),
    [draft, setDraft] = useState<Memory | null>(null),
    [ann, setAnn] = useState<Memory | null>(null),
    [current, setCurrent] = useState<Memory | null>(null),
    [year, setYear] = useState(new Date().getFullYear()),
    [filter, setFilter] = useState('全部'),
    [busy, setBusy] = useState(false),
    [message, setMessage] = useState(''),
    [loadError, setLoadError] = useState(''),
    [signin, setSignin] = useState(false),
    [calendarOpen, setCalendarOpen] = useState(false),
    [calendarDate, setCalendarDate] = useState(today()),
    [cameraOpen, setCameraOpen] = useState(false),
    [cameraError, setCameraError] = useState(''),
    [menu, setMenu] = useState(false),
    [connection, setConnection] = useState<'off' | 'connecting' | 'on'>('off'),
    [deviceState, setDeviceState] = useState<DeviceState>({
      connected: false,
      always_on: false,
      audio_enabled: false,
    }),
    [companion, setCompanion] = useState<Memory | null>(null),
    [companionEventId, setCompanionEventId] = useState(''),
    [companionTrigger, setCompanionTrigger] = useState<'touch' | 'voice'>(
      'touch',
    ),
    [uploading, setUploading] = useState(false),
    [uploadProgress, setUploadProgress] = useState(''),
    [pendingFiles, setPendingFiles] = useState<File[]>([]),
    [ritualLit, setRitualLit] = useState(false);
  const galleryInput = useRef<HTMLInputElement>(null),
    avatarInput = useRef<HTMLInputElement>(null),
    captureInput = useRef<HTMLInputElement>(null),
    video = useRef<HTMLVideoElement>(null),
    stream = useRef<MediaStream | null>(null),
    lock = useRef(false),
    screenRef = useRef(screen),
    deviceEventSince = useRef(Date.now() - 5000),
    activeDeviceEvent = useRef(''),
    heading = useRef<HTMLHeadingElement>(null),
    uploadLock = useRef(false);
  useEffect(() => {
    screenRef.current = screen;
  }, [screen]);
  type ArchivePayload = {
    archive: Archive | null;
    version: number;
    error?: string;
  };
  type SavePayload = { version: number; error?: string };
  const name = archive.name || '它';
  const syncArchive = async (value: Archive) => {
    try {
      await fetch('/api/device/archive', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(value),
      });
    } catch {}
  };
  function go(next: number, replace = false) {
    if (replace)
      history.replaceState({ pawlight: true, screen: next }, '', `#${next}`);
    else history.pushState({ pawlight: true, screen: next }, '', `#${next}`);
    setScreen(next);
    setMessage('');
    window.scrollTo({ top: 0 });
  }
  async function load() {
    setLoadError('');
    setSignin(false);
    try {
      const response = await fetch('/api/archive', { cache: 'no-store' });
      if (response.status === 401) {
        setSignin(true);
        setScreen(2);
        return;
      }
      const data = (await response.json()) as ArchivePayload;
      if (!response.ok) throw Error(data.error);
      if (data.archive) {
        setArchive(data.archive);
        void syncArchive(data.archive);
        setVersion(data.version);
        const years = data.archive.events.map((e: Memory) =>
          Number(e.date.slice(0, 4)),
        );
        if (years.length) setYear(Math.max(...years));
      }
      let draftData;
      try {
        draftData = JSON.parse(
          sessionStorage.getItem('pawlight-draft') || 'null',
        );
      } catch {}
      if (draftData) {
        setDraft(draftData.draft ?? null);
        setAnn(draftData.ann ?? null);
      }
      go(data.archive?.ready ? 11 : 2, true);
    } catch (e) {
      setLoadError(e instanceof Error ? e.message : '加载失败，请检查网络');
    }
  }
  useEffect(() => {
    const timer = setTimeout(load, 1100);
    return () => clearTimeout(timer);
  }, []);
  useEffect(() => {
    const fn = (e: PopStateEvent) => {
      const s = e.state?.screen;
      const allowed = [2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17];
      setCalendarOpen(false);
      setCameraOpen(false);
      if (allowed.includes(s)) {
        if ([6, 7].includes(s) && !draft) go(5, true);
        else if ([9, 10].includes(s) && !ann) go(8, true);
        else if ([12, 13, 14].includes(s) && !current) go(11, true);
        else if (s === 15 && !companion) go(11, true);
        else setScreen(s);
      } else go(archive.ready ? 11 : 2, true);
    };
    window.addEventListener('popstate', fn);
    return () => window.removeEventListener('popstate', fn);
  }, [draft, ann, current, archive.ready, companion]);
  useEffect(() => {
    try {
      sessionStorage.setItem('pawlight-draft', JSON.stringify({ draft, ann }));
    } catch {}
  }, [draft, ann]);
  useEffect(() => {
    heading.current?.focus({ preventScroll: true });
  }, [screen]);
  useEffect(() => {
    if (!cameraOpen) return;
    let cancelled = false;
    setCameraError('');
    (async () => {
      try {
        if (!navigator.mediaDevices?.getUserMedia)
          throw Error('当前浏览器无法直接使用相机，请用手机拍照入口。');
        const s = await navigator.mediaDevices.getUserMedia({
          video: { facingMode: 'environment' },
          audio: false,
        });
        if (cancelled) {
          s.getTracks().forEach((t) => t.stop());
          return;
        }
        stream.current = s;
        if (video.current) {
          video.current.srcObject = s;
          await video.current.play();
        }
      } catch (e) {
        setCameraError(
          e instanceof Error ? `相机未开启：${e.message}` : '无法开启相机',
        );
      }
    })().catch(() => {});
    return () => {
      cancelled = true;
      stream.current?.getTracks().forEach((t) => t.stop());
      stream.current = null;
    };
  }, [cameraOpen]);
  useEffect(() => {
    if (!archive.ready) return;
    let stopped = false;
    let running = false;
    async function pollDevice() {
      if (running || stopped) return;
      running = true;
      try {
        const stateResponse = await fetch('/api/device/state', {
          cache: 'no-store',
        });
        if (stateResponse.ok) {
          const state = (await stateResponse.json()) as DeviceState;
          if (!stopped) {
            setDeviceState(state);
            setConnection(state.connected ? 'on' : 'off');
            setRitualLit(state.always_on);
          }
        } else if (!stopped) {
          setConnection('off');
          setDeviceState({
            connected: false,
            always_on: false,
            audio_enabled: false,
          });
        }
        const eventResponse = await fetch('/api/device/events', {
          cache: 'no-store',
        });
        if (eventResponse.ok) {
          const data = (await eventResponse.json()) as {
            events: DeviceEvent[];
          };
          const event =
            screenRef.current === 15
              ? undefined
              : [...(data.events || [])].reverse().find((item) => {
                  if (
                    item.type !== 'memory_recalled' &&
                    item.type !== 'companion_triggered'
                  )
                    return false;
                  const created = Date.parse(item.created_at);
                  return (
                    Number.isFinite(created) &&
                    created >= deviceEventSince.current
                  );
                });
          if (event && !stopped && event.id !== activeDeviceEvent.current) {
            const created = Date.parse(event.created_at);
            if (Number.isFinite(created))
              deviceEventSince.current = Math.max(
                deviceEventSince.current,
                created + 1,
              );
            activeDeviceEvent.current = event.id;
            setCompanionEventId(event.id);
            const trigger =
              event.type === 'memory_recalled'
                ? 'voice'
                : event.trigger || 'touch';
            setCompanionTrigger(trigger);
            const recalledTitle = event.memory_title || '一段回忆';
            setCompanion({
              ...blankMemory('companion'),
              id: `companion-${event.id}`,
              title:
                event.type === 'memory_recalled'
                  ? `想起「${recalledTitle}」`
                  : trigger === 'voice'
                    ? `你呼唤了${name}`
                    : `你轻触了${name}`,
              text:
                event.transcript ||
                (event.touch_kind === 'slow'
                  ? '你轻轻抚摸着它，安静地陪伴了一会儿。'
                  : '你碰了碰它的小屋，又想起了在一起的时光。'),
              tags: [emotionTag[event.emotion] || '想念'],
              createdAt: event.created_at || new Date().toISOString(),
            });
            go(15);
          }
        }
      } catch {
        if (!stopped) {
          setConnection('off');
          setDeviceState({
            connected: false,
            always_on: false,
            audio_enabled: false,
          });
        }
      } finally {
        running = false;
      }
    }
    void pollDevice();
    const timer = setInterval(pollDevice, 1200);
    return () => {
      stopped = true;
      clearInterval(timer);
    };
  }, [archive.ready]);
  const stateRef = useRef({ archive, year });
  stateRef.current = { archive, year };
  useEffect(() => {
    const context = (
      document as Document & {
        modelContext?: {
          registerTool: (t: unknown, o: unknown) => Promise<void> | void;
        };
      }
    ).modelContext;
    if (!context) return;
    const controller = new AbortController();
    const tools = [
      {
        name: 'read_memory_timeline',
        description: 'Read saved memories and the selected year.',
        inputSchema: {
          type: 'object',
          properties: {},
          additionalProperties: false,
        },
        annotations: { readOnlyHint: true, untrustedContentHint: true },
        execute: () => stateRef.current,
      },
      {
        name: 'show_memory_year',
        description:
          'Open the memory timeline for a selected year without changing saved memories.',
        inputSchema: {
          type: 'object',
          properties: {
            year: { type: 'integer', minimum: 1900, maximum: 2100 },
          },
          required: ['year'],
          additionalProperties: false,
        },
        annotations: { readOnlyHint: false, untrustedContentHint: false },
        execute: (input: unknown) => {
          const y = (input as { year: number })?.year;
          if (!Number.isInteger(y) || y < 1900 || y > 2100)
            throw Error('Year must be between 1900 and 2100');
          if (!stateRef.current.archive.ready)
            throw Error('Create a pet profile first');
          setYear(y);
          go(11);
          return {
            year: y,
            events: stateRef.current.archive.events.filter((e) =>
              e.date.startsWith(String(y)),
            ).length,
          };
        },
      },
    ];
    for (const tool of tools)
      try {
        Promise.resolve(
          context.registerTool(tool, { signal: controller.signal }),
        ).catch(() => {});
      } catch {}
    return () => controller.abort();
  }, []);
  async function persist(next: Archive) {
    const response = await fetch('/api/archive', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ archive: next, version }),
    });
    const data = (await response.json()) as SavePayload;
    if (!response.ok) throw Error(data.error || '暂时无法保存');
    setArchive(next);
    setVersion(data.version);
    void syncArchive(next);
    return next;
  }
  async function run(task: () => Promise<void>) {
    if (lock.current) return;
    lock.current = true;
    setBusy(true);
    setMessage('');
    try {
      await task();
    } catch (e) {
      setMessage(e instanceof Error ? e.message : '操作失败，请重试');
    } finally {
      lock.current = false;
      setBusy(false);
    }
  }
  async function resetDemo() {
    if (
      !window.confirm(
        '确定重置 App 吗？宠物名字、时间线和已上传的照片都会清空。',
      )
    )
      return;
    await run(async () => {
      const response = await fetch('/api/archive', { method: 'DELETE' });
      const data = (await response.json()) as { error?: string };
      if (!response.ok) throw Error(data.error || '暂时无法重置');
      sessionStorage.removeItem('pawlight-draft');
      setArchive({ name: '', ready: false, events: [], avatar: '' });
      void syncArchive({ name: '', ready: false, events: [], avatar: '' });
      setVersion(0);
      setDraft(null);
      setAnn(null);
      setCurrent(null);
      setCompanion(null);
      setRitualLit(false);
      setMenu(false);
      history.replaceState({ pawlight: true, screen: 1 }, '', '#1');
      setScreen(1);
      setTimeout(() => go(2, true), 1100);
    });
  }
  async function saveEvent(event: Memory, target = 11) {
    await run(async () => {
      const item = {
        ...event,
        title:
          event.title.trim() ||
          (event.kind === 'memory' ? '一段温暖的记忆' : '重要的日子'),
      };
      const next = {
        ...archive,
        events: [...archive.events.filter((e) => e.id !== item.id), item],
      };
      await persist(next);
      if (item.kind === 'companion' && companionEventId) {
        try {
          const acknowledged = await fetch(
            `/api/device/events/${encodeURIComponent(companionEventId)}`,
            { method: 'POST' },
          );
          if (acknowledged.ok) {
            setCompanionEventId('');
            activeDeviceEvent.current = '';
          }
        } catch {}
      }
      setYear(Number(item.date.slice(0, 4)));
      setFilter('全部');
      setCurrent(item);
      if (item.kind === 'anniversary') setAnn(null);
      else setDraft(null);
      go(target);
      setMessage('已添加到时间线');
    });
  }
  function startMemory() {
    if (!draft) setDraft(blankMemory());
    go(5);
  }
  function startAnniversary() {
    if (!ann) setAnn({ ...blankMemory('anniversary'), title: '' });
    go(8);
  }
  function back() {
    const map: Record<number, number> = {
      3: 2,
      4: 3,
      5: 11,
      6: 5,
      7: 6,
      8: 11,
      9: 8,
      10: 9,
      12: 11,
      13: 12,
      14: 11,
      15: 11,
      17: 4,
    };
    go(map[screen] ?? 11);
  }
  async function upload(files: File[]) {
    if (uploadLock.current || !draft) return;
    uploadLock.current = true;
    setUploading(true);
    setMessage('');
    let count = draft.media.length;
    const room = 9 - count;
    const candidates = files.slice(0, room),
      success: Media[] = [],
      failed: File[] = [];
    let issues =
      files.length > room
        ? `最多可选择 9 个照片或视频，已保留前 ${room} 个。`
        : '';
    try {
      for (let i = 0; i < candidates.length; i++) {
        const file = candidates[i];
        setUploadProgress(`正在上传 ${i + 1}/${candidates.length}`);
        if (file.size > 50 * 1024 * 1024) {
          issues += ' 单个文件不能超过 50 MB。';
          continue;
        }
        if (!/^(image|video)\//.test(file.type)) {
          issues += ' 有文件不是支持的照片或视频。';
          continue;
        }
        try {
          const r = await fetch('/api/media', {
            method: 'POST',
            headers: {
              'Content-Type': file.type,
              'X-File-Name': encodeURIComponent(file.name),
            },
            body: file,
          });
          const item = (await r.json()) as Media & { error?: string };
          if (!r.ok) throw Error(item.error);
          success.push(item);
          count++;
        } catch (e) {
          failed.push(file);
          issues += ` ${file.name}：${e instanceof Error ? e.message : '上传失败'}`;
        }
      }
      setDraft((d) =>
        d ? { ...d, media: [...d.media, ...success].slice(0, 9) } : d,
      );
      setPendingFiles(failed);
      setMessage(issues || `已上传 ${success.length} 个文件`);
    } finally {
      setUploading(false);
      uploadLock.current = false;
      setUploadProgress('');
    }
  }
  async function uploadAvatar(file: File | undefined) {
    if (!file || uploadLock.current) return;
    if (!file.type.startsWith('image/')) {
      setMessage('请选择照片文件');
      return;
    }
    if (file.size > 50 * 1024 * 1024) {
      setMessage('照片不能超过 50 MB');
      return;
    }
    uploadLock.current = true;
    setUploading(true);
    setMessage('');
    try {
      const response = await fetch('/api/media', {
        method: 'POST',
        headers: {
          'Content-Type': file.type,
          'X-File-Name': encodeURIComponent(file.name),
        },
        body: file,
      });
      const item = (await response.json()) as Media & { error?: string };
      if (!response.ok) throw Error(item.error || '上传失败');
      setArchive((value) => ({ ...value, avatar: item.url }));
      setMessage('宠物照片已更新');
    } catch (error) {
      setMessage(error instanceof Error ? error.message : '上传失败，请重试');
    } finally {
      uploadLock.current = false;
      setUploading(false);
    }
  }
  function choose(files: FileList | null) {
    if (files) void upload(Array.from(files));
  }
  async function takePhoto() {
    if (!video.current?.videoWidth) {
      setCameraError('相机还没准备好，请稍候');
      return;
    }
    const c = document.createElement('canvas');
    c.width = video.current.videoWidth;
    c.height = video.current.videoHeight;
    c.getContext('2d')!.drawImage(video.current, 0, 0);
    c.toBlob(
      (blob) => {
        if (blob) {
          setCameraOpen(false);
          void upload([
            new File([blob], `拍照-${Date.now()}.jpg`, { type: 'image/jpeg' }),
          ]);
        }
      },
      'image/jpeg',
      0.92,
    );
  }
  function openEvent(e: Memory) {
    setCurrent(e);
    go(12);
  }
  async function refreshConnection() {
    setConnection('connecting');
    try {
      const response = await fetch('/api/device/state', { cache: 'no-store' });
      const state = (await response.json()) as DeviceState & { error?: string };
      if (!response.ok) throw Error(state.error || '无法连接硬件网关');
      setDeviceState(state);
      setConnection(state.connected ? 'on' : 'off');
      setRitualLit(state.always_on);
      if (!state.connected) setMessage('网关已启动，但暂未检测到 ESP32');
    } catch (error) {
      setConnection('off');
      setMessage(error instanceof Error ? error.message : '无法连接硬件网关');
    }
  }
  async function setAlwaysOn(enabled: boolean) {
    await run(async () => {
      const response = await fetch('/api/device/mode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ always_on: enabled }),
      });
      const state = (await response.json()) as DeviceState & { error?: string };
      if (!response.ok) throw Error(state.error || '无法切换长明灯');
      setDeviceState(state);
      setConnection(state.connected ? 'on' : 'off');
      setRitualLit(state.always_on);
      setMessage(
        enabled
          ? '长明灯已开启，语音识别已暂停'
          : '长明灯已关闭，语音识别已恢复',
      );
    });
  }
  async function sendCurrentEffect() {
    if (!current) return;
    await run(async () => {
      const response = await fetch('/api/device/effect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          emotion:
            (
              {
                温暖: 'warm',
                快乐: 'happy',
                平静: 'calm',
                想念: 'miss',
                难过: 'sad',
              } as Record<string, string>
            )[current.tags[0]] || 'warm',
          hold_ms: 12000,
        }),
      });
      const result = (await response.json()) as {
        ok?: boolean;
        error?: string;
      };
      if (!response.ok || !result.ok)
        throw Error(result.error || 'ESP32 尚未连接');
      go(14);
    });
  }
  const isTimeline = screen === 11 || screen === 16;
  const titles: Record<number, string> = {
    2: '建立宠物档案',
    3: '确认宠物身份',
    4: `${name}的纪念空间`,
    5: '上传一段记忆',
    6: '这段记忆是什么感觉？',
    7: '确认记忆',
    8: '设置重要日期',
    9: `选择${ann?.title === '自定义日期' ? '重要日期' : ann?.title || '日期'}时间`,
    10: '重要日期',
    11: `${name}的记忆`,
    12: current?.title || '记忆',
    13: '发送记忆光点',
    14: '发送成功',
    15: '新的陪伴记录',
    16: `${name}的记忆`,
    17: '为思念，点一盏灯',
  };
  const subtitles: Record<number, string> = {
    2: '为它，留下一本关于陪伴的故事',
    3: `这是你记忆里的${name}吗？`,
    4: '把想念，安放在每一个日常里',
    5: `留住${name}的模样，也留住那一刻的温暖。`,
    6: `关于${name}的每一种心情，都可以留在这里。`,
    7: '再看一眼，把这一刻好好收藏',
    8: '记住每一个特别的日子',
    9: '选一个日子，让思念有温柔的归期',
    10: '把重要的日子，好好留在这里',
    13: `让这一刻化成温柔的光，陪伴在${name}身边`,
    15: `${name}的小屋，刚刚感受到了你的想念`,
    17: `轻轻触碰，让${name}化作一束温柔的光`,
  };
  const visible = archive.events
    .filter(
      (e) =>
        Number(e.date.slice(0, 4)) === year &&
        (filter === '全部' ||
          (filter === '照片记忆'
            ? e.kind === 'memory'
            : filter === '纪念日'
              ? e.kind === 'anniversary'
              : e.kind === 'companion')),
    )
    .sort(
      (a, b) =>
        b.date.localeCompare(a.date) || b.createdAt.localeCompare(a.createdAt),
    );
  let content: React.ReactNode = null,
    footer: React.ReactNode = null;
  if (screen === 1)
    return (
      <main className="phone splash">
        <img src={`${A}logo.png`} alt="PawLight" />
        <h1>PawLight</h1>
        <p role="status">
          {loadError ? '暂时无法加载你的记忆' : '正在翻开我们的故事…'}
        </p>
        {loadError && (
          <>
            <p className="error">{loadError}</p>
            <Action onClick={load}>重新加载</Action>
          </>
        )}
      </main>
    );
  if (signin)
    return (
      <main className="phone">
        <header>
          <h1>为陪伴，留一盏光</h1>
          <p>登录后，你的照片和回忆会保存在专属空间。</p>
        </header>
        <img className="hero" src={`${A}profile.png`} alt="小狗和小猫" />
        <a
          className="primary signin"
          href="/signin-with-chatgpt?return_to=%2F"
          target="_top"
        >
          登录并开始
        </a>
        <img className="grass" src={`${A}grass.svg`} alt="" />
      </main>
    );
  if (screen === 2) {
    content = (
      <>
        <div className="profile-hero">
          <img src={`${A}profile.png`} alt="手绘小狗和小猫" />
        </div>
        <label className="name-label">
          它叫什么名字？
          <input
            autoComplete="off"
            value={archive.name}
            maxLength={40}
            onChange={(e) =>
              setArchive({
                ...archive,
                name: Array.from(e.target.value).slice(0, 20).join(''),
              })
            }
            onKeyDown={(e) => {
              if (e.key === 'Enter' && archive.name.trim()) go(3);
            }}
            placeholder="输入宠物名字"
          />
        </label>
      </>
    );
    footer = (
      <Action
        disabled={!archive.name.trim()}
        onClick={() => {
          setArchive({ ...archive, name: archive.name.trim() });
          go(3);
        }}
      >
        继续
      </Action>
    );
  }
  if (screen === 3) {
    content = (
      <div className="identity">
        <input
          ref={avatarInput}
          className="hidden"
          type="file"
          accept="image/*"
          onChange={(event) => {
            void uploadAvatar(event.target.files?.[0]);
            event.currentTarget.value = '';
          }}
        />
        <button
          className="avatar-picker"
          type="button"
          disabled={uploading}
          onClick={() => avatarInput.current?.click()}
          aria-label="从相册选择宠物照片"
        >
          <img
            src={archive.avatar || `${A}portrait.png`}
            alt={`${name}的宠物照片`}
          />
        </button>
        <p>名字</p>
        <h2>{name}</h2>
      </div>
    );
    footer = <Action onClick={() => go(4)}>确认</Action>;
  }
  if (screen === 4) {
    content = (
      <>
        <div className="entry-cards">
          <div>
            <Icon name="add" size={80} />
            <h2>照片记忆</h2>
            <p>珍藏一起的时光</p>
          </div>
          <div>
            <Icon name="calendar" size={80} />
            <h2>纪念日</h2>
            <p>记住特别的日子</p>
          </div>
        </div>
        <p className="center gentle">陪伴，会以另一种方式继续。</p>
      </>
    );
    footer = (
      <Action
        disabled={busy}
        onClick={() =>
          run(async () => {
            await persist({
              ...archive,
              ready: true,
              events: archive.events.length ? archive.events : seedEvents(),
            });
            setRitualLit(false);
            go(17);
          })
        }
      >
        {busy ? '正在建立空间…' : '开始使用'}
      </Action>
    );
  }
  if (screen === 17) {
    const ritualWords = '它已经变成了光，从此每晚陪你入睡';
    content = (
      <div className={`light-ritual ${ritualLit ? 'is-lit' : ''}`}>
        <img className="ritual-decor" src={`${A}decor-hearts.png`} alt="" />
        <button
          className="ritual-lamp"
          type="button"
          aria-label={ritualLit ? '纪念灯已点亮' : '点亮纪念灯'}
          aria-pressed={ritualLit}
          disabled={busy}
          onClick={() => void setAlwaysOn(!ritualLit)}
        >
          <span className="lamp-flame" />
          <span className="lamp-glow" />
          <span className="lamp-body">
            <Icon name="paw" size={30} />
          </span>
        </button>
        {!ritualLit && <p className="ritual-hint">轻触灯芯，开启平静长明灯</p>}
        {ritualLit && (
          <div className="ritual-message">
            <img src={`${A}decor-highlight.png`} alt="" />
            <output className="ritual-words" aria-label={ritualWords}>
              {Array.from(ritualWords).map((character, index) => (
                <span
                  key={`${character}-${index}`}
                  style={{ animationDelay: `${index * 90}ms` }}
                >
                  {character}
                </span>
              ))}
            </output>
          </div>
        )}
      </div>
    );
    footer = (
      <div className="actions">
        <Action
          secondary
          disabled={busy}
          onClick={() => void setAlwaysOn(!ritualLit)}
        >
          {ritualLit ? '关闭长明灯' : '开启长明灯'}
        </Action>
        <Action onClick={() => go(11)}>进入记忆时间线</Action>
      </div>
    );
  }
  if (screen === 5 && draft) {
    content = (
      <>
        <input
          ref={galleryInput}
          className="hidden"
          type="file"
          accept="image/*,video/*"
          multiple
          onChange={(e) => {
            choose(e.target.files);
            e.target.value = '';
          }}
        />
        <input
          ref={captureInput}
          className="hidden"
          type="file"
          accept="image/*"
          capture="environment"
          onChange={(e) => {
            choose(e.target.files);
            e.target.value = '';
          }}
        />
        {draft.media.length ? (
          <>
            <Gallery
              media={draft.media}
              remove={
                uploading
                  ? undefined
                  : (i) =>
                      setDraft({
                        ...draft,
                        media: draft.media.filter((_, n) => n !== i),
                      })
              }
            />
            <p className="counter">{draft.media.length}/9 个照片或视频</p>
          </>
        ) : (
          <Button
            className="upload-area"
            onClick={() => galleryInput.current?.click()}
          >
            <Icon name="add" size={86} />
            <h2>添加照片</h2>
            <p>选一段影像，让记忆有迹可循</p>
          </Button>
        )}
        <Button
          className="choice peach"
          disabled={uploading || draft.media.length >= 9}
          onClick={() => setCameraOpen(true)}
        >
          <Icon name="camera" size={44} />
          拍照
        </Button>
        <Button
          className="choice sage"
          disabled={uploading || draft.media.length >= 9}
          onClick={() => galleryInput.current?.click()}
        >
          <Icon name="album" size={44} />
          从相册中选择
        </Button>
        <p className="hint">照片和视频合计最多 9 个，每个不超过 50 MB。</p>
        {uploading && (
          <p role="status">
            {uploadProgress}
            <progress />
          </p>
        )}
        {pendingFiles.length > 0 && !uploading && (
          <Button className="secondary" onClick={() => upload(pendingFiles)}>
            重试失败的 {pendingFiles.length} 个文件
          </Button>
        )}
      </>
    );
    footer = (
      <Action
        disabled={uploading || draft.media.length === 0}
        onClick={() => go(6)}
      >
        下一步
      </Action>
    );
  }
  if (screen === 6 && draft) {
    content = (
      <>
        <div className="tags">
          {feelings.map((t) => (
            <Button
              key={t}
              className={`tag ${draft.tags.includes(t) ? 'selected' : ''}`}
              aria-pressed={draft.tags.includes(t)}
              onClick={() =>
                setDraft({
                  ...draft,
                  tags: draft.tags.includes(t)
                    ? draft.tags.filter((x) => x !== t)
                    : [...draft.tags, t],
                })
              }
            >
              {draft.tags.includes(t) && (
                <img className="tag-paw" src={`${A}decor-paw.png`} alt="" />
              )}
              {t}
            </Button>
          ))}
        </div>
        <label>
          时间
          <Button
            className="date-field"
            onClick={() => {
              setCalendarDate(draft.date);
              setCalendarOpen(true);
            }}
          >
            <Icon name="calendar" />
            {dateLabel(draft.date)}
            <span>›</span>
          </Button>
        </label>
        <label>
          记忆标题 <small>选填</small>
          <input
            maxLength={40}
            value={draft.title}
            onChange={(e) => setDraft({ ...draft, title: e.target.value })}
            placeholder="给这段记忆起个名字"
          />
        </label>
        <label>
          写下这段回忆 <small>选填</small>
          <textarea
            value={draft.text}
            onChange={(e) =>
              setDraft({
                ...draft,
                text: Array.from(e.target.value).slice(0, 500).join(''),
              })
            }
            placeholder="例如：那天阳光很好……"
          />
          <span className="counter">{Array.from(draft.text).length}/500</span>
        </label>
      </>
    );
    footer = <Action onClick={() => go(7)}>下一步</Action>;
  }
  if (screen === 7 && draft) {
    content = (
      <>
        <h3>照片</h3>
        {draft.media.length ? (
          <Gallery media={draft.media} />
        ) : (
          <p>这是一段文字记忆</p>
        )}
        <div className="feeling-card">
          <small>这段记忆的感觉</small>
          <h3>{draft.tags.join(' · ') || '静静珍藏'}</h3>
          <p>{dateLabel(draft.date)}</p>
        </div>
        {draft.title && <h2>{draft.title}</h2>}
        <h3>写下的话</h3>
        <blockquote>{draft.text || '有些陪伴，不需要言语。'}</blockquote>
      </>
    );
    footer = (
      <Action disabled={busy} onClick={() => saveEvent(draft)}>
        {busy ? '正在保存…' : '保存记忆'}
      </Action>
    );
  }
  if (screen === 8 && ann) {
    content = (
      <>
        {anniversaryKinds.map((t, i) => (
          <Button
            key={t}
            className={`choice ${i % 2 ? 'sage' : 'peach'} ${ann.title === t ? 'chosen' : ''}`}
            aria-pressed={ann.title === t}
            onClick={() => setAnn({ ...ann, title: t })}
          >
            <Icon name="calendar" size={50} />
            {t}
            {ann.title === t && <span className="right">✓</span>}
          </Button>
        ))}
      </>
    );
    footer = (
      <Action disabled={!ann.title} onClick={() => go(9)}>
        下一步
      </Action>
    );
  }
  if (screen === 9 && ann) {
    content = (
      <>
        {ann.title === '自定义日期' && (
          <label>
            纪念日名称
            <input
              maxLength={40}
              placeholder="例如：第一次旅行"
              value={ann.text}
              onChange={(e) => setAnn({ ...ann, text: e.target.value })}
            />
          </label>
        )}
        <div className="date-summary">
          <Icon name="calendar" size={50} />
          <div>
            <small>{ann.title}</small>
            <h2>{dateLabel(ann.date)}</h2>
          </div>
        </div>
        <Calendar
          value={ann.date}
          onChange={(date) => setAnn({ ...ann, date })}
        />
        <div className="reminder-row">
          <div>
            <h3>是否提醒</h3>
            <p>当天上午 9:00 提醒我</p>
          </div>
          <Switch
            className="reminder-switch"
            checked={ann.reminder}
            onCheckedChange={(reminder) => setAnn({ ...ann, reminder })}
            aria-label="是否提醒"
          />
        </div>
        {ann.reminder && (
          <p className="hint">保存后可加入手机日历，开启每年提醒。</p>
        )}
      </>
    );
    footer = (
      <Action
        disabled={ann.title === '自定义日期' && !ann.text.trim()}
        onClick={() => go(10)}
      >
        保存
      </Action>
    );
  }
  if (screen === 10 && ann) {
    const other = archive.events.filter(
      (e) => e.kind === 'anniversary' && e.id !== ann.id,
    );
    content = (
      <>
        {[...other, ann].map((e, i) => (
          <div className={`choice ${i % 2 ? 'sage' : 'peach'}`} key={e.id}>
            <Icon name="calendar" size={44} />
            <div>
              <h3>{e.title === '自定义日期' ? e.text : e.title}</h3>
              <p>
                {dateLabel(e.date)}
                {e.reminder ? ' · 每年提醒' : ''}
              </p>
            </div>
          </div>
        ))}
      </>
    );
    footer = (
      <Action
        disabled={busy}
        onClick={() =>
          saveEvent({
            ...ann,
            title: ann.title === '自定义日期' ? ann.text : ann.title,
            text: '',
          })
        }
      >
        {busy ? '正在保存…' : '保存'}
      </Action>
    );
  }
  if (isTimeline) {
    content = (
      <>
        <div className="years">
          <Button
            className="plain"
            aria-label="上一年"
            disabled={year === 1900}
            onClick={() => setYear(year - 1)}
          >
            ‹
          </Button>
          {Array.from({ length: 5 }, (_, i) => year - 2 + i)
            .filter((y) => y >= 1900 && y <= 2100)
            .map((y) => (
              <Button
                className={`year ${y === year ? 'active' : ''}`}
                aria-pressed={y === year}
                onClick={() => setYear(y)}
                key={y}
              >
                {y}
              </Button>
            ))}
          <Button
            className="plain"
            aria-label="下一年"
            disabled={year === 2100}
            onClick={() => setYear(year + 1)}
          >
            ›
          </Button>
        </div>
        <div className="filters">
          {['全部', '照片记忆', '陪伴记录', '纪念日'].map((t) => (
            <Button
              className={`filter ${filter === t ? 'active' : ''}`}
              key={t}
              aria-pressed={filter === t}
              onClick={() => setFilter(t)}
            >
              {t}
            </Button>
          ))}
        </div>
        {visible.length ? (
          <div className="timeline">
            {visible.map((e, i) => (
              <button
                className={`timeline-event ${i % 2 ? 'reverse' : ''}`}
                key={e.id}
                onClick={() => openEvent(e)}
                aria-label={`查看${e.title}，${dateLabel(e.date)}`}
              >
                <div className="event-visual">
                  {e.media[0] ? (
                    e.media[0].type.startsWith('video/') ? (
                      <div className="video-cover">
                        ▷<span>视频记忆</span>
                      </div>
                    ) : (
                      <img src={e.media[0].url} alt={e.title} />
                    )
                  ) : (
                    <Icon
                      name={e.kind === 'companion' ? 'wave' : 'calendar'}
                      size={64}
                    />
                  )}
                </div>
                <span className={`node ${e.kind}`} />
                <div className="event-copy">
                  <h3>{e.title}</h3>
                  <time>{dateLabel(e.date)}</time>
                  <p>
                    {e.kind === 'companion'
                      ? '陪伴记录 · 想念'
                      : e.text || '一个值得珍藏的日子'}
                  </p>
                  {e.reminder && <span className="tiny-tag">已设置提醒</span>}
                </div>
              </button>
            ))}
          </div>
        ) : (
          <div className="empty">
            <img src={`${A}profile.png`} alt="小狗和小猫" />
            <h2>这一页，等你写下</h2>
            <p>
              {year} 年{filter === '全部' ? '还没有记忆' : `还没有${filter}`}
              <br />
              从一张照片，或一个特别的日子开始。
            </p>
            {archive.events.length > 0 && (
              <Button
                className="text-button"
                onClick={() => {
                  setYear(
                    Math.max(
                      ...archive.events.map((e) => Number(e.date.slice(0, 4))),
                    ),
                  );
                  setFilter('全部');
                }}
              >
                查看最近的记忆
              </Button>
            )}
          </div>
        )}
        <p className="hint center">
          触碰实体宠物灯或完成一次语音对话，会自动记录新的陪伴
        </p>
      </>
    );
    footer = (
      <div className="actions">
        <Action onClick={startAnniversary}>添加纪念日</Action>
        <Action onClick={startMemory}>添加记忆</Action>
      </div>
    );
  }
  if (screen === 12 && current) {
    content = (
      <>
        {current.media.length ? (
          <Gallery media={current.media} />
        ) : (
          <div className="event-hero">
            <Icon
              name={current.kind === 'companion' ? 'wave' : 'calendar'}
              size={100}
            />
          </div>
        )}
        <h2 className="date-heading">
          <Icon name="calendar" />
          {dateLabel(current.date)}
        </h2>
        <div className="tags compact">
          <span className="tag selected">
            {current.kind === 'memory'
              ? '照片记忆'
              : current.kind === 'companion'
                ? '陪伴记录'
                : '纪念日'}
          </span>
          {current.tags.map((t) => (
            <span className="tag" key={t}>
              {t}
            </span>
          ))}
        </div>
        <h3>那一天的记忆</h3>
        <blockquote>
          {current.text || '每一个被记住的日子，都有你的身影。'}
        </blockquote>
        {current.reminder && (
          <Button
            className="secondary"
            onClick={() => downloadReminder(current)}
          >
            加入系统日历提醒
          </Button>
        )}
      </>
    );
    footer = (
      <div className="actions">
        <Action
          secondary
          onClick={() => {
            setDraft(structuredClone(current));
            go(6);
          }}
        >
          编辑记忆
        </Action>
        <Action onClick={() => go(13)}>发送为光点</Action>
      </div>
    );
  }
  if (screen === 13 && current) {
    content = (
      <>
        <div className="memory-mini">
          {current.media[0] ? (
            <MediaView media={current.media[0]} />
          ) : (
            <Icon name="calendar" size={60} />
          )}
          <div>
            <h2>{current.title}</h2>
            <p>唤醒关键词：{current.tags[0] || current.title.slice(0, 6)}</p>
          </div>
        </div>
        <h3>光点预览</h3>
        <div className={`glow ${current.tags.includes('平静') ? 'teal' : ''}`}>
          <span />
        </div>
        <Button
          className={`choice device ${connection === 'on' ? 'sage' : ''}`}
          disabled={connection === 'connecting'}
          onClick={() => void refreshConnection()}
        >
          <Icon name="urn" size={44} />
          <div>
            <h3>
              {connection === 'on'
                ? '光宠已连接'
                : connection === 'connecting'
                  ? '正在连接小屋…'
                  : '连接光宠'}
            </h3>
            <p>
              {name}的小屋 ·{' '}
              {connection === 'on'
                ? deviceState.always_on
                  ? '长明灯模式'
                  : '语音模式'
                : '点击刷新设备状态'}
            </p>
          </div>
          {connection === 'on' && <span className="right">✓</span>}
        </Button>
        <p className="hint center">
          由本机网关连接 ESP32，设备需与电脑处于同一网络
        </p>
      </>
    );
    footer = (
      <Action
        disabled={connection !== 'on' || busy}
        onClick={() => void sendCurrentEffect()}
      >
        {busy ? '正在发送…' : '发送'}
      </Action>
    );
  }
  if (screen === 14 && current) {
    content = (
      <div className="success-content">
        <div className="glow success">
          <img src={`${A}success.svg`} alt="发送成功" />
        </div>
        <h2>“{current.title}”已化为光点</h2>
        <p>它会在{name}的小屋里，静静闪耀。</p>
        <div className="feeling-card">
          {current.title} · {current.tags[0] || '温暖'}光点<p>模拟发送已完成</p>
        </div>
      </div>
    );
    footer = <Action onClick={() => go(11)}>返回时间线</Action>;
  }
  if (screen === 15 && companion) {
    content = (
      <>
        <div className="companion-card">
          <p>
            {dateLabel(companion.date)}　
            {new Date(companion.createdAt).toLocaleTimeString('zh-CN', {
              hour: '2-digit',
              minute: '2-digit',
            })}
          </p>
          <hr />
          <div className="companion-heading">
            <Icon name="wave" size={70} />
            <div>
              <h2>{companion.title}</h2>
              <p>
                {companionTrigger === 'voice'
                  ? '一次倾诉被温柔地记录下来'
                  : '一次触碰化作了新的陪伴'}
              </p>
            </div>
          </div>
          <hr />
          <p>
            触发方式：
            {companionTrigger === 'voice' ? '语音对话' : '触碰实体宠物灯'}
          </p>
        </div>
        <h3>这次陪伴来自</h3>
        <div className="tags compact">
          <span className="tag">
            {companionTrigger === 'voice' ? '语音' : '触摸'}
          </span>
          <span className="tag selected">{companion.tags[0] || '想念'}</span>
        </div>
        <blockquote>
          {companion.text || '每一次呼唤，都是仍在继续的陪伴。'}
        </blockquote>
      </>
    );
    footer = (
      <Action disabled={busy} onClick={() => saveEvent(companion, 16)}>
        {busy ? '正在保存…' : '添加到时间线'}
      </Action>
    );
  }
  return (
    <>
      <main
        className={`phone screen-${screen} ${isTimeline ? 'timeline-screen' : ''}`}
      >
        <div className="topbar">
          {![2, 11, 16].includes(screen) ? (
            <Button
              className="back plain"
              disabled={busy || uploading}
              onClick={back}
              aria-label="返回"
            >
              <Icon name="back" size={40} />
            </Button>
          ) : (
            <span />
          )}
          {isTimeline && (
            <Button
              className="more plain"
              aria-label="更多选项"
              onClick={() => setMenu(true)}
            >
              •••
            </Button>
          )}
        </div>
        <header
          className={
            isTimeline || [13, 15].includes(screen) ? 'align-left' : ''
          }
        >
          <div className="title-row">
            <h1 ref={heading} tabIndex={-1}>
              {titles[screen]}
            </h1>
            {isTimeline && (
              <img className="title-paw" src={`${A}decor-paw.png`} alt="" />
            )}
            {screen === 4 && (
              <img
                className="title-highlight"
                src={`${A}decor-highlight.png`}
                alt=""
              />
            )}
          </div>
          {subtitles[screen] && <p>{subtitles[screen]}</p>}
          {[5, 6, 7, 8, 10].includes(screen) && (
            <img className="hearts" src={`${A}decor-hearts.png`} alt="" />
          )}
        </header>
        <section className="content">{content}</section>
        {message && (
          <div className="notice" role="status">
            {message}
          </div>
        )}
        <footer>{footer}</footer>
        <img className="grass" src={`${A}grass.svg`} alt="" />
      </main>
      <Dialog open={calendarOpen} onOpenChange={setCalendarOpen}>
        <DialogContent className="modal" showCloseButton={false}>
          <DialogTitle>选择记忆时间</DialogTitle>
          <DialogDescription>选择年份、月份和日期</DialogDescription>
          <Calendar value={calendarDate} onChange={setCalendarDate} />
          <div className="actions">
            <Action secondary onClick={() => setCalendarOpen(false)}>
              取消
            </Action>
            <Action
              disabled={!validDate(calendarDate)}
              onClick={() => {
                if (draft) setDraft({ ...draft, date: calendarDate });
                setCalendarOpen(false);
              }}
            >
              保存
            </Action>
          </div>
        </DialogContent>
      </Dialog>
      <Dialog open={cameraOpen} onOpenChange={setCameraOpen}>
        <DialogContent className="modal" showCloseButton={false}>
          <DialogTitle>拍一张照片</DialogTitle>
          <DialogDescription>留住此刻的温暖</DialogDescription>
          <video
            className="camera-preview"
            ref={video}
            autoPlay
            playsInline
            muted
          />
          {cameraError && <p className="error">{cameraError}</p>}
          <div className="actions">
            <Action secondary onClick={() => setCameraOpen(false)}>
              返回
            </Action>
            <Action onClick={takePhoto}>拍照</Action>
          </div>
          <Button
            className="text-button"
            onClick={() => {
              setCameraOpen(false);
              captureInput.current?.click();
            }}
          >
            使用手机系统相机
          </Button>
        </DialogContent>
      </Dialog>
      <Dialog open={menu} onOpenChange={setMenu}>
        <DialogContent className="modal" showCloseButton={false}>
          <DialogTitle>更多选项</DialogTitle>
          <DialogDescription>管理光宠和本地纪念空间。</DialogDescription>
          <Button
            className="secondary"
            onClick={() => {
              setMenu(false);
              go(17);
            }}
          >
            {deviceState.always_on ? '查看长明灯' : '开启长明灯'}
          </Button>
          <Button
            className="secondary danger"
            disabled={busy}
            onClick={resetDemo}
          >
            重置 App
          </Button>
        </DialogContent>
      </Dialog>
    </>
  );
}
