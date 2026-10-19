// One-off: expand `A | B` signatures into one declaration per alternative (unions retired,
// design principles 2.13). The declarations keep their body, so the operator a signature
// stands for is chosen by its argument types as before.
import fs from "node:fs";

const files = [
    "stdlib/signatures/algo.cml",
    "stdlib/signatures/globals.cml",
    "stdlib/signatures/json.cml",
    "stdlib/signatures/math.cml",
    "stdlib/signatures/statistics.cml",
];

const special = {
    sqrt: [
        ["i32", "f32"],
        ["int", "float"],
        ["f32", "f32"],
        ["float", "float"],
    ],
    len_string_or_array: null, // handled by the generic rule (string | T[]) is not a plain list
};

function splitTop(text, sep) {
    const parts = [];
    let depth = 0;
    let start = 0;
    for (let i = 0; i < text.length; ++i) {
        const ch = text[i];
        if (ch === "(" || ch === "[" || ch === "<" || ch === "{") {
            ++depth;
        } else if (ch === ")" || ch === "]" || ch === ">" || ch === "}") {
            --depth;
        } else if (ch === sep && depth === 0) {
            parts.push(text.slice(start, i));
            start = i + 1;
        }
    }
    parts.push(text.slice(start));
    return parts.map((part) => part.trim());
}

function splitParams(text) {
    const inner = text.slice(1, -1).trim();
    if (inner === "") {
        return [];
    }
    return splitTop(inner, ",");
}

// `(A | B)[]` is a union inside an array: its alternatives are the element types.
function typeAlternatives(type) {
    let suffix = "";
    let core = type.trim();
    if (core.startsWith("(")) {
        const close = core.indexOf(")");
        suffix = core.slice(close + 1);
        core = core.slice(1, close);
    }
    const alternatives = splitTop(core, "|").filter(Boolean);
    return alternatives.map((alternative) => alternative + suffix);
}

const isUnion = (type) => splitTop(type, "|").length > 1;

function expand(name, params, ret, body) {
    const alternativesByParam = params.map((param) => typeAlternatives(param.type));
    const retAlternatives = typeAlternatives(ret);

    if (name === "sqrt") {
        return special.sqrt.map(([arg, result]) => ({
            fixed: params.map(() => arg),
            result,
        }));
    }
    const anySpecialParam = alternativesByParam.some((alternatives) =>
        alternatives.some((alternative) => /Map<|Set<|Json|string|^T\[/.test(alternative)),
    );
    if (anySpecialParam) {
        // A union of differently shaped types (`string | T[]`, `Map<K, V> | Set<T>`, `Json |
        // Json?`): one declaration per alternative, the return paired when it is a union of
        // the same number of alternatives.
        const count = params.reduce((n, _, i) => Math.max(n, alternativesByParam[i].length), 1);
        const out = [];
        for (let i = 0; i < count; ++i) {
            const fixed = params.map((param, p) => {
                const alternatives = alternativesByParam[p];
                return alternatives.length > 1 ? alternatives[i] : param.type;
            });
            const result =
                retAlternatives.length > 1
                    ? retAlternatives[Math.min(i, retAlternatives.length - 1)]
                    : ret;
            out.push({ fixed, result });
        }
        return out;
    }
    if (retAlternatives.length > 1) {
        // The alternatives of the result name the alternatives of the arguments: every
        // argument takes the same one, as the equal sets say.
        return retAlternatives.map((alternative) => ({
            fixed: params.map(() => alternative),
            result: alternative,
        }));
    }
    // A fixed result: one declaration per alternative of the argument that has them.
    const varying = alternativesByParam.findIndex((alternatives) => alternatives.length > 1);
    const out = [];
    for (const alternative of alternativesByParam[varying]) {
        out.push({
            fixed: params.map((param, i) => (i === varying ? alternative : param.type)),
            result: ret,
        });
    }
    return out;
}

for (const file of files) {
    const lines = fs.readFileSync(file, "utf8").split("\n");
    const out = [];
    for (let i = 0; i < lines.length; ++i) {
        const line = lines[i];
        const match = /^inner func (\w+)\((.*)\): (.*) \{$/.exec(line);
        if (match === null || (!line.includes("|"))) {
            out.push(line);
            continue;
        }
        const [, name, paramsText, ret] = match;
        const body = lines[i + 1];
        if (!/^\s+return /.test(body)) {
            throw new Error(`unexpected body for ${name}: ${body}`);
        }
        // The declaration's closing `}` is the next line.
        const close = lines[i + 2];
        if (close.trim() !== "}") {
            throw new Error(`unexpected end for ${name}: ${close}`);
        }
        const params = splitParams(`(${paramsText})`).map((text) => {
            const at = text.indexOf(":");
            return { name: text.slice(0, at).trim(), type: text.slice(at + 1).trim() };
        });
        const variants = expand(name, params, ret, body);
        for (const variant of variants) {
            const paramText = params
                .map((param, p) => `${param.name}: ${variant.fixed[p]}`)
                .join(", ");
            out.push(`inner func ${name}(${paramText}): ${variant.result} {`);
            out.push(body);
            out.push("}");
        }
        i += 2;
    }
    fs.writeFileSync(file, out.join("\n"));
}