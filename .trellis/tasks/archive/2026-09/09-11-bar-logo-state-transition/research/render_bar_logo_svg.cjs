// 只做离屏 SVG 检查；属性来自 C++ 生产 ApplyAttributes 导出，不重写主题公式。
const fs = require("node:fs");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const assert = require("node:assert/strict");
const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, value, index, all) => {
    if (index % 2 === 0) pairs.push([value, all[index + 1]]);
    return pairs;
}, []));
assert(args["--profile"], "--profile must be produced by InkeysHeadlessTests --bar-logo-profile-output");
const sharp = require(args["--sharp"] || "sharp");
const root = path.resolve(args["--root"] || process.cwd());
const output = path.resolve(args["--output"] || path.join(root, "Build/BarLogoValidation"));
const profile = JSON.parse(fs.readFileSync(args["--profile"], "utf8"));
const shared = fs.readFileSync(path.join(root, "Inkeys/src/UI/logo1.svg"), "utf8");
const oldBase = execFileSync("git", ["-C", root, "show", "4478887c:Inkeys/src/UI/logo1.svg"], { encoding: "utf8" });
const oldInk = execFileSync("git", ["-C", root, "show", "4478887c:Inkeys/src/UI/Frame 94.svg"], { encoding: "utf8" });
const oldLight = execFileSync("git", ["-C", root, "show", "0606cbe0:Inkeys/src/UI/logo2.svg"], { encoding: "utf8" });
const escapeRegex = text => text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
const pixel = (image, x, y) => [...image.data.subarray((y * image.info.width + x) * 4, (y * image.info.width + x) * 4 + 4)];
const rgb = color => [1, 3, 5].map(index => Number.parseInt(color.slice(index, index + 2), 16));
const atSize = (svg, size) => svg.replace(/<svg\b[^>]*>/, tag => tag.replace(/\bwidth="[^"]*"/, `width="${size}"`).replace(/\bheight="[^"]*"/, `height="${size}"`));
const raw = async source => sharp(Buffer.from(source)).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
const tintOld = (source, color) => source.replaceAll("rgba(10,0,7,0)", color).replaceAll("rgba(9,0,2,0)", "#232B2F");

function prepare(sample) {
    let svg = shared;
    for (const [id, attribute, value] of sample.attributes) {
        assert(/^[\w.-]+$/.test(id) && /^[\w.-]+$/.test(attribute) && !/["<>]/.test(value), "unexpected SVG property");
        const tagPattern = new RegExp(`<[^>]+\\bid="${escapeRegex(id)}"[^>]*>`, "g");
        const tags = svg.match(tagPattern);
        assert.equal(tags?.length, 1, `${sample.id}: missing or ambiguous ${id}`);
        const attributePattern = new RegExp(`\\s${escapeRegex(attribute)}="[^"]*"`);
        const original = tags[0];
        const replacement = attributePattern.test(original)
            ? original.replace(attributePattern, ` ${attribute}="${value}"`)
            : original.replace(/\/?>(?=$)/, ending => ` ${attribute}="${value}"${ending}`);
        svg = svg.replace(original, replacement);
    }
    return svg;
}

function difference(actual, expected) {
    let max = 0, sum = 0, affected = 0;
    for (let index = 0; index < actual.data.length; index += 4) {
        let changed = false;
        for (let channel = 0; channel < 4; ++channel) {
            // 比较预乘色，透明边缘的任意 RGB 不应被误认为视觉误差。
            const a = channel === 3 ? actual.data[index + 3] : actual.data[index + channel] * actual.data[index + 3] / 255;
            const b = channel === 3 ? expected.data[index + 3] : expected.data[index + channel] * expected.data[index + 3] / 255;
            const delta = Math.abs(a - b);
            max = Math.max(max, delta);
            sum += delta;
            changed ||= delta > 1;
        }
        affected += changed ? 1 : 0;
    }
    return { max, mean: sum / actual.data.length, affectedPixels: affected };
}

async function oldDark(color, drawing, size) {
    if (drawing === 0) return raw(atSize(oldBase, size));
    const base = await sharp(Buffer.from(atSize(oldBase, size))).png().toBuffer();
    return sharp(base).composite([{ input: await sharp(Buffer.from(atSize(tintOld(oldInk, color), size))).png().toBuffer() }])
        .ensureAlpha().raw().toBuffer({ resolveWithObject: true });
}

async function main() {
    fs.mkdirSync(output, { recursive: true });
    assert.equal(profile.samples.length, 150, "all 6 colors x 5 material x 5 drawing samples required");
    const neutralByWeight = new Map();
    const screenByWeight = new Map();
    const rendered = new Map();
    const differences = [];
    const penInterior = [[158, 72], [137, 120], [111, 171], [100, 196]];
    const cuts = [[150, 86], [153, 87], [156, 89], [115, 159], [118, 160], [120, 161]];
    for (const sample of profile.samples) {
        const svg = prepare(sample);
        const image = await raw(svg);
        rendered.set(sample.id, { svg, image, sample });
        fs.writeFileSync(path.join(output, `${sample.id}.svg`), svg);
        if (sample.drawing === 0) {
            if (neutralByWeight.has(sample.light))
                assert.deepEqual(image.data, neutralByWeight.get(sample.light), `${sample.id}: remembered color leaks into non-drawing`);
            neutralByWeight.set(sample.light, image.data);
        }
        if (sample.light === 0 && (sample.drawing === 0 || sample.drawing === 1)) {
            // 原大小、常规 80 DIP 和 2x DPI 都使用真正的旧资源作对照。
            for (const size of [80, 160, 256]) {
                const current = size === 256 ? image : await raw(atSize(svg, size));
                const baseline = await oldDark(sample.actual, sample.drawing, size);
                const delta = difference(current, baseline);
                // 同一栅格化阶段的原始节点合成必须逐像素一致，先排除真正的形状/颜色差异。
                const inner = source => source.slice(source.indexOf(">") + 1, source.lastIndexOf("</svg>"));
                const originalNodes = sample.drawing === 0 ? oldBase
                    : `<svg width="256" height="256" viewBox="0 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">${inner(oldBase)}${inner(tintOld(oldInk, sample.actual))}</svg>`;
                const sameStage = await raw(atSize(originalNodes, size));
                const sameStageDelta = difference(current, sameStage);
                assert.deepEqual(current.data, sameStage.data, `${sample.id}/${size}: original Dark SVG paint changed`);
                differences.push({ sample: sample.id, size, originalNodes: sameStageDelta, separateBitmaps: delta });
                // 原来两次 8-bit 位图转换有微小预乘舍入差；严格限制最大值和全图平均值。
                assert(delta.max <= 3 && delta.mean < 0.10,
                    `${sample.id}/${size}: original Dark mismatch ${JSON.stringify(delta)}`);
                await sharp(baseline.data, { raw: baseline.info }).png().toFile(path.join(output, `${sample.id}-${size}-4478887c.png`));
                await sharp(current.data, { raw: current.info }).png().toFile(path.join(output, `${sample.id}-${size}-current.png`));
            }
        }
        if (sample.light === 1 && sample.drawing === 1) {
            for (const [x, y] of penInterior)
                assert.deepEqual(pixel(image, x, y), [...rgb(sample.actual), 255], `${sample.id}: whole pen actual RGB`);
            for (const [x, y] of cuts)
                assert.equal(pixel(image, x, y)[3], 0, `${sample.id}: internal gap has a transverse stroke`);
            const screen = pixel(image, 70, 100);
            if (screenByWeight.has(1)) assert.deepEqual(screen, screenByWeight.get(1), "Light screen follows pen color");
            screenByWeight.set(1, screen);
            const delta = difference(image, await raw(tintOld(oldLight, sample.actual)));
            assert(delta.mean < 0.01, `${sample.id}: retained Light endpoint changed ${JSON.stringify(delta)}`);
        }
    }
    // 端点与三个中间材质帧并排，并分别显示非绘制/绘制状态。
    const tile = 112, labelHeight = 22, columns = 6, rows = 10;
    const tiles = [], labels = [];
    for (let lightIndex = 0; lightIndex < 5; ++lightIndex)
        for (let drawingIndex of [0, 4])
            for (let colorIndex = 0; colorIndex < 6; ++colorIndex) {
                const row = lightIndex * 2 + (drawingIndex === 4 ? 1 : 0);
                const entry = rendered.get(`c${colorIndex}-l${lightIndex}-d${drawingIndex}`);
                const background = lightIndex === 0 ? "#262626" : "#F4F6F7";
                const png = await sharp({ create: { width: tile, height: tile, channels: 4, background } })
                    .composite([{ input: await sharp(Buffer.from(entry.svg)).resize(tile, tile).png().toBuffer() }]).png().toBuffer();
                tiles.push({ input: png, left: colorIndex * tile, top: row * (tile + labelHeight) });
                labels.push(`<text x="${colorIndex * tile + 3}" y="${row * (tile + labelHeight) + tile + 14}">${entry.sample.label} L${entry.sample.light} D${entry.sample.drawing}</text>`);
            }
    const width = tile * columns, height = (tile + labelHeight) * rows;
    const labelSvg = `<svg width="${width}" height="${height}"><g font-family="Arial" font-size="10" fill="#263238">${labels.join("")}</g></svg>`;
    await sharp({ create: { width, height, channels: 4, background: "#FFFFFF" } })
        .composite([...tiles, { input: Buffer.from(labelSvg), left: 0, top: 0 }])
        .png().toFile(path.join(output, "logo-state-contact-sheet.png"));
    fs.writeFileSync(path.join(output, "dark-baseline-differences.json"), JSON.stringify(differences, null, 2) + "\n");
    console.log("PASS production attributes: 150 states; original Dark composite; Light true RGB/gaps/outline; neutral independent of history");
    console.log(path.join(output, "logo-state-contact-sheet.png"));
}

main().catch(error => { console.error(error); process.exitCode = 1; });
