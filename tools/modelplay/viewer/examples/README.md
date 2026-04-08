# How to run examples

1. Build slippi-viewer: `npm run build`
2. Run an HTTP server to serve static files from the project root. This can be done with `python3 -m http.server`. This is needed for 2 reasons:
  - The viewer component expects to be able to fetch the character zips. The examples are configured such that they are fetched locally from the same server.
  - If you want to try fetching the zips from elsewhere by changing the `zips-base-url` attribute on the `<slippi-viewer>` component, the fetch request will need to have an origin set for CORS purposes. Running index.html via the `file://` protocol (double clicking it in the file browser) will not fulfill this requirement, but running the local server does. (The server will need to have CORS configured, of course.)
3. Navigate to the desired example in the web browser, for example: `http://localhost8000/examples/replay/index.html`.
  - Make sure the port is correct; 8000 is used here because it's the default for the Python HTTP server.
