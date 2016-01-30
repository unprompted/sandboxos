"use strict";

var kUnchecked = "☐";
var kChecked = "☑";

imports.core.register("onInput", function(command) {
	if (command.substring(0, "action:".length) == "action:") {
		command = JSON.parse(command.substring("action:".length));
		if (command.action == "set") {
			setItem(command.key, command.item, command.value).then(notifyChanged).then(redisplay);
		} else if (command.action == "remove") {
			removeItem(command.key, command.item).then(notifyChanged).then(redisplay);
		}
	} else {
		addItem("todo", command).then(notifyChanged).then(redisplay);
	}
});

imports.core.register("onMessage", function(message) {
	return redisplay();
});

function notifyChanged() {
	return imports.core.broadcast({changed: true});
}

function readList(key) {
	return imports.database.get(key).catch(function(error) {
		return null;
	}).then(function(todo) {
		if (!todo) {
			todo = JSON.stringify({name: "TODO", items: []});
		}
		return JSON.parse(todo);
	});
}

function writeList(key, todo) {
	return imports.database.set(key, JSON.stringify(todo));
}

function addItem(key, name) {
	return readList(key).then(function(todo) {
		todo.items.push({name: name, value: false});
		return writeList(key, todo);
	});
}

function setItem(key, name, value) {
	return readList(key).then(function(todo) {
		for (var i = 0; i < todo.items.length; i++) {
			if (todo.items[i].name == name) {
				todo.items[i].value = value;
			}
		}
		return writeList(key, todo);
	});
}

function removeItem(key, name) {
	return readList(key).then(function(todo) {
		todo.items = todo.items.filter(function(item) {
			return item.name != name;
		});
		return writeList(key, todo);
	});
}

function printList(name, key, items) {
	imports.terminal.print(name);
	imports.terminal.print("=".repeat(name.length));
	for (var i = 0; i < items.length; i++) {
		var isChecked = items[i].value;
		var style = ["", "text-decoration: line-through"];
		imports.terminal.print(
			{command: "action:" + JSON.stringify({action: "set", key: key, item: items[i].name, value: !isChecked}), value: isChecked ? kChecked : kUnchecked},
			" ",
			{style: style[isChecked ? 1 : 0], value: items[i].name},
			" (",
			{command: "action:" + JSON.stringify({action: "remove", key: key, item: items[i].name}), value: "x"},
			")");
	}
}

function redisplay() {
	imports.terminal.clear();
	imports.terminal.setEcho(false);
	var key = "todo";
	readList(key).then(function(todo) {
		printList(todo.name, key, todo.items);
	}).catch(function(error) {
		imports.terminal.print("ERROR: " + error);
	});
}

redisplay();
