import { sqliteTable, text, integer } from 'drizzle-orm/sqlite-core';
export const archives=sqliteTable('archives',{owner:text('owner').primaryKey(),data:text('data').notNull(),version:integer('version').notNull().default(0)});
export const media=sqliteTable('media',{id:text('id').primaryKey(),owner:text('owner').notNull(),type:text('type').notNull(),name:text('name').notNull()});
