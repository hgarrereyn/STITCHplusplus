import { defineCollection } from "astro:content";
import { z } from "astro/zod";
import { docsLoader } from "@astrojs/starlight/loaders";
import { docsSchema } from "@astrojs/starlight/schema";

export const collections = {
  docs: defineCollection({
    loader: docsLoader(),
    // `languages: true` shows the tutorial language picker under the page title.
    schema: docsSchema({ extend: z.object({ languages: z.boolean().default(false) }) }),
  }),
};
