import { build } from "esbuild";
import { solidPlugin } from "esbuild-plugin-solid";

import fs from "node:fs";
import path from "node:path";
import child_process from "node:child_process"
import util from "node:util";

const args = process.argv.slice(2);
// const watch = args.includes('--watch');
const deploy = args.includes('--deploy');

// Step 1: Build worker

await build({
  entryPoints: ["src/worker/worker.ts"],
  outdir: "build",
  bundle: true,
  minify: deploy,
  logLevel: "info",
});

// Step 2: Build slippi-viewer with injected web worker

const exec = util.promisify(child_process.exec);

// Because a web component is being created, it cannot be styled by a CSS file
// which will be included somewhere such as an index.html. Instead, the styles
// must be directly added to the component within a <styles> tag, which means
// importing them as text.
// Because Tailwind is being used, meaning index.css must be compiled, and
// recompiled to only include the necessary classes, this is added to the build
// pipeline as a plugin here, to generate/minify all CSS before it is imported.

const buildCssPlugin = {
  name: 'buildCssPlugin',
  setup(build) {
    build.onStart(async () => {
      const cssFiles = fs.readdirSync("src/css");
      const execPromises = [];
      for (const file of cssFiles) {
        console.log(`Building ${file}...`);
        execPromises.push(exec(`npx tailwindcss -c tailwind.config.js -i src/css/${file} -o build/css/${file} --minify`));
      }
      await Promise.all(execPromises);
    });

    build.onResolve({ filter: /\.css/ }, args => {
      const fileName = path.basename(args.path);
      return { path: path.join(import.meta.dirname, "build/css", fileName) }
    });

    build.onEnd(() => {
      fs.rmSync("build", { recursive: true, force: true });
    });
  },
}

// @shelacek/ubjson resolves TextEncoder/TextDecoder with a ternary operator
// to conditionally require the classes from node's "util" module. However,
// esbuild does not recognize this, and tries to resolve it since it assumes
// it is being unconditionally loaded. This plugin marks these specific
// requires as external, which makes esbuild not try to resolve them.
//
// It is worth noting that esbuild will correctly mark requires wrapped in
// try/catch as external. It may be worthwhile to submit a PR to
// @shelacek/ubjson to change the resolution method such that it works
// as expected. (Or, to look at submitting an esbuild PR to do the same.)

const skipUbjsonUtilResolutionPlugin = {
  name: "skipUbjsonUtilResolutionPlugin",
  setup(build) {
    build.onResolve({ filter: /^util$/ }, args => {
      if (args.resolveDir.endsWith("@shelacek/ubjson/dist")) {
        return { external: true };
      }
    });
  }
};

// Some hacking needs to be done to make Web Workers work in esbuild:
// https://github.com/evanw/esbuild/issues/312
//
// Specifically, the extra challenge here is that we aren't sure to have access
// to something like a /worker.js script on the server, since this web component
// can be embedded wherever. To get around this, an inline worker is created
// by taking the worker code directly, creating an object URL from it,
// constructing the worker, then revoking the temporary URL, which is done in
// workerUtil.ts.
//
// The extra complexity comes from the fact that the worker has to be bundled
// too. So we do this as the first step of the build, and then point to the
// bundled worker file (as text) when the worker is imported later.

const workerPlugin = {
  name: "workerPlugin",
  setup(build) {
    build.onResolve({ filter: /\/worker$/ }, async () => {
      return {
        path: path.join(import.meta.dirname, "build/worker.js"),
        namespace: "worker"
      };
    });
    build.onLoad({ filter: /\/worker.js$/, namespace: "worker" }, async (args) => {
      return {
        loader: "text" ,
        contents: await fs.promises.readFile(args.path)
      };
    });
  }
}

const buildOptions = {
  entryPoints: ["src/index.tsx"],
  outdir: "dist/",
  bundle: true,
  minify: deploy,
  loader: {
    ".svg": "dataurl",
    ".css": "text"
  },
  logLevel: "info",
  plugins: [solidPlugin(), buildCssPlugin, skipUbjsonUtilResolutionPlugin, workerPlugin],
}

build(buildOptions);
