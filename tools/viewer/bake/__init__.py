"""Silhouette bake: re-render fighter animations to slippilab-style SVG frames.

The existing viewer zips came from slippilab's create-animations pipeline
(Maya orthographic side camera, 100 units wide at 1000 px, root locked at the
image centre, frames 0..200, potrace outlines). This package reproduces that
contract directly from the extracted DATs, with the fighter's model-part
visibility applied per subaction frame so accessory meshes appear.
"""
