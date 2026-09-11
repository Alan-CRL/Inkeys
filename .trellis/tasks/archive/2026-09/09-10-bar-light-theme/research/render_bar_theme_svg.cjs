// 只做 SVG 离屏资源验证；不创建 HWND，也不替代产品 D2D 的整栏视觉验收。
const fs = require("node:fs");
const path = require("node:path");
const assert = require("node:assert/strict");

const argumentsByName = Object.fromEntries(process.argv.slice(2).reduce((pairs, value, index, all) => {
    if (index % 2 === 0) pairs.push([value, all[index + 1]]);
    return pairs;
}, []));
const sharp = require(argumentsByName["--sharp"] || "sharp");
const root = path.resolve(argumentsByName["--root"] || process.cwd());
const output = path.resolve(argumentsByName["--output"] || path.join(root, "Build/ThemeSvgValidation"));
const assets = path.join(root, "Inkeys/src/UI");
const svgSource = name => fs.readFileSync(path.join(assets, name), "utf8");
const light = svgSource("logo2.svg");
const screen = svgSource("logo1.svg");
const ink = svgSource("Frame 94.svg");
const colors = [
    ["Black", "#000000"], ["White", "#FFFFFF"], ["Pale yellow", "#FFF4A3"],
    ["Red", "#FF1000"], ["Blue", "#326ED9"], ["Custom", "#704895"],
];
const palette = argumentsByName["--palette"]
    ? JSON.parse(fs.readFileSync(argumentsByName["--palette"], "utf8")).samples
    : null;
const recolor = (svg, color) => svg.replaceAll("rgba(10,0,7,0)", color)
    .replaceAll("rgba(9,0,2,0)", "#192124");
const rgb = color => [1, 3, 5].map(index => Number.parseInt(color.slice(index, index + 2), 16));
const pixel = (image, x, y) => [...image.data.subarray((y * image.info.width + x) * 4,
    (y * image.info.width + x) * 4 + 4)];
const raw = async source => sharp(Buffer.from(source)).ensureAlpha().raw().toBuffer({ resolveWithObject: true });

async function main() {
    fs.mkdirSync(output, { recursive: true });
    const penInterior = [[158, 72], [137, 120], [111, 171], [100, 196]];
    // 两个分节横截面的内部采样必须完全透明，防止闭合路径描边横穿笔杆。
    const internalCuts = [[150, 86], [153, 87], [156, 89], [115, 159], [118, 160], [120, 161]];
    let screenPixel;
    for (const [label, color] of colors) {
        const image = await raw(recolor(light, color));
        for (const [x, y] of penInterior) {
            assert.deepEqual(pixel(image, x, y), [...rgb(color), 255], `${label}: whole pen RGB`);
        }
        for (const [x, y] of internalCuts) {
            assert.equal(pixel(image, x, y)[3], 0, `${label}: internal cut has no transverse outline`);
        }
        const currentScreen = pixel(image, 70, 100);
        if (screenPixel) assert.deepEqual(currentScreen, screenPixel, `${label}: screen is independent`);
        screenPixel = currentScreen;
        const darkInk = await raw(recolor(ink, color));
        assert.equal(pixel(darkInk, 70, 100)[3], 0, `${label}: dark ink cannot tint screen`);
        for (const [x, y] of penInterior) {
            assert.deepEqual(pixel(darkInk, x, y), [...rgb(color), 255], `${label}: full dark pen layer`);
        }
    }

    const tileWidth = 208;
    const tileHeight = 220;
    const width = tileWidth * colors.length;
    const height = tileHeight * 2;
    const tiles = [];
    const labels = [];
    for (let row = 0; row < 2; ++row) {
        const background = row === 0 ? "#F4F6F7" : "#262626";
        for (let column = 0; column < colors.length; ++column) {
            const [label, actualColor] = colors[column];
            // 深色显示色由无窗口 C++ 入口导出，脚本不复制产品提亮算法。
            const color = row === 1 && palette ? palette[column].dark : actualColor;
            const svg = row === 0 ? recolor(light, color) : recolor(ink, color);
            const x = column * tileWidth;
            const y = row * tileHeight;
            const sources = row === 0 ? [svg] : [screen, svg];
            const large = await sharp({ create: { width: 144, height: 144, channels: 4, background } })
                .composite(await Promise.all(sources.map(async source => ({
                    input: await sharp(Buffer.from(source)).resize(144, 144).png().toBuffer(),
                    top: 0, left: 0,
                })))).png().toBuffer();
            const actual = await sharp({ create: { width: 80, height: 80, channels: 4, background } })
                .composite(await Promise.all(sources.map(async source => ({
                    input: await sharp(Buffer.from(source)).resize(80, 80).png().toBuffer(),
                    top: 0, left: 0,
                })))).png().toBuffer();
            tiles.push({ input: large, top: y + 8, left: x + 8 });
            tiles.push({ input: actual, top: y + 128, left: x + 120 });
            labels.push(`<text x="${x + 12}" y="${y + 200}" fill="${row === 0 ? "#53616A" : "#EEEEEE"}">${label}</text>`);
        }
    }
    const text = `<svg width="${width}" height="${height}"><g font-family="Arial" font-size="15">${labels.join("")}</g></svg>`;
    const backdrop = `<svg width="${width}" height="${height}"><rect width="${width}" height="${tileHeight}" fill="#F4F6F7"/><rect y="${tileHeight}" width="${width}" height="${tileHeight}" fill="#262626"/></svg>`;
    await sharp(Buffer.from(backdrop)).composite([...tiles, { input: Buffer.from(text), top: 0, left: 0 }])
        .png().toFile(path.join(output, "logo-resources.png"));
    console.log("PASS offscreen SVG: six pen colors, four full-pen sections, two unoutlined gaps, independent screen");
    console.log(path.join(output, "logo-resources.png"));
}

main().catch(error => { console.error(error); process.exitCode = 1; });
