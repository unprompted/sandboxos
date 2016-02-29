"use strict";

// Start at bottom left facing up.
// Height = 20.  Width = 10.
// 10 between.

var letters = {
	A: 'fd(20); rt(90); fd(10); rt(90); fd(10); rt(90); fd(10); pu(); bk(10); lt(90); pd(); fd(10); pu(); lt(90); fd(10); lt(90); pd();',
	D: 'fd(20); rt(90); fd(10); rt(70); fd(11); rt(40); fd(11); rt(70); fd(10); pu(); bk(20); rt(90); pd();',
	E: 'pu(); fd(20); rt(90); fd(10); lt(180); pd(); fd(10); lt(90); fd(10); lt(90); fd(8); pu(); rt(180); fd(8); lt(90); pd(); fd(10); lt(90); fd(10); pu(); fd(10); lt(90); pd()',
	H: 'fd(20); pu(); bk(10); pd(); rt(90); fd(10); lt(90); pu(); fd(10); rt(180); pd(); fd(20); pu(); lt(90); fd(10); lt(90); pd();',
	L: 'pu(); fd(20); rt(180); pd(); fd(20); lt(90); fd(10); pu(); fd(10); lt(90); pd();',
	O: 'fd(20); rt(90); fd(10); rt(90); fd(20); rt(90); fd(10); pu(); bk(20); rt(90); pd();',
	R: 'fd(20); rt(90); fd(10); rt(90); fd(10); rt(90); fd(10); pu(); bk(8); lt(90); pd(); fd(10); pu(); lt(90); fd(12); lt(90); pd();',
	W: 'pu(); fd(20); rt(180); pd(); fd(20); lt(90); fd(5); lt(90); fd(12); rt(180); pu(); fd(12); pd(); lt(90); fd(5); lt(90); fd(20); pu(); bk(20); rt(90); fd(10); lt(90); pd();',
	' ': 'pu(); rt(90); fd(20); lt(90); pd();',
};

function render(text) {
	terminal.clear();
	terminal.print(text, " using ", {href: "http://codeheartjs.com/turtle/"}, ".");
	var contents = '<script src="http://codeheartjs.com/turtle/turtle.min.js">-*- javascript -*-</script><script>\n';
	contents += 'setScale(2); setWidth(5);\n';
	for (var i = 0; i < text.length; i++) {
		var c = text.charAt(i).toUpperCase();
		if (letters[c]) {
			contents += letters[c] + '\n';
		} else {
			contents += letters[' '] + '\n';
		}
	}
	contents += "ht();\n";
	contents += "window.addEventListener('message', function(event) { console.debug(event.data); }, false);\n";
	contents += "</script>\n";
	terminal.print({iframe: contents, width: 640, height: 480});
	terminal.print("Type text and the letters ", {style: "color: #ff0", value: Object.keys(letters).join("")}, " in it will be drawn.");
}

render("Hello, world!");

core.register("onInput", render);