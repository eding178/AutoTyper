#include "../src/input_plan.h"
#include <cassert>
#include <iostream>

int main() {
    using typer::Kind;
    auto text = typer::makePlan(u"áñ€中文😀");
    assert(text && text.strokes.size() == 6);
    assert(text.strokes[0].first == 0x00e1);
    assert(text.strokes[1].first == 0x00f1);
    assert(text.strokes[5].units == 2);
    assert(text.strokes[5].first == 0xd83d && text.strokes[5].second == 0xde00);

    auto lines = typer::makePlan(u"a\r\nb\nc\rd\t ");
    assert(lines && lines.strokes.size() == 9);
    for (int i : {1, 3, 5}) assert(lines.strokes[i].kind == Kind::Enter);
    assert(lines.strokes[7].kind == Kind::Tab);
    assert(lines.strokes[8].first == u' ');
    assert(typer::makePlan(u"a\r\n").strokes.size() == 2);
    assert(typer::makePlan(u"\r\n\n\r").strokes.size() == 3);
    assert(typer::makePlan(u"text").strokes.back().kind == Kind::Unicode);
    assert(typer::makePlan(u"").strokes.empty());

    for (char16_t bad : {char16_t(0), char16_t(8), char16_t(27), char16_t(127),
                         char16_t(0x80), char16_t(0x9f), char16_t(0xd800), char16_t(0xdc00)}) {
        std::u16string invalid = u"ok";
        invalid += bad;
        const auto plan = typer::makePlan(invalid);
        assert(!plan && plan.strokes.empty() && plan.errorOffset == 2);
    }
    assert(!typer::makePlan(std::u16string{char16_t(0xd800), u'a'}));
    auto extremes = typer::makePlan(std::u16string{char16_t(0xd800), char16_t(0xdc00),
                                                 char16_t(0xdbff), char16_t(0xdfff)});
    assert(extremes && extremes.strokes.size() == 2);
    auto longText = typer::makePlan(std::u16string(1000000, u'x'));
    assert(longText && longText.strokes.size() == 1000000);
    std::cout << "PASS: Unicode, surrogate pairs, newline normalization, tabs, controls, empty and long text\n";
}
