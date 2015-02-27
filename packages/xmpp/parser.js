function XmlStreamParser() {
	this.buffer = "";
	this._parsed = [];
	this.reset();
	return this;
}

XmlStreamParser.kText = "text";
XmlStreamParser.kElement = "element";
XmlStreamParser.kEndElement = "endElement";
XmlStreamParser.kAttributeName = "attributeName";
XmlStreamParser.kAttributeValue = "attributeValue";

XmlStreamParser.prototype.reset = function() {
	this._state = XmlStreamParser.kText;
	this._attributes = {};
	this._attributeName = "";
	this._attributeValue = "";
	this._attributeEquals = false;
	this._attributeQuote = "";
	this._slash = false;
	this._value = "";
	this._decl = false;
}

XmlStreamParser.prototype.parse = function(data) {
	this._parsed = [];
	
	for (var i = 0; i < data.length; i++) {
		var c = data.charAt(i);
		this.parseCharacter(c);
	}
	
	return this._parsed;
}

XmlStreamParser.prototype.flush = function() {
	var node = {type: this._state};
	if (this._value) {
		node.value = this._value;
	}
	if (this._attributes.length || this._state == XmlStreamParser.kElement) {
		node.attributes = this._attributes;
	}
	if (this._state != XmlStreamParser.kText || this._value) {
		this.emit(node);
	}
	this.reset();
}

XmlStreamParser.prototype.parseCharacter = function(c) {
	switch (this._state) {
	case XmlStreamParser.kText:
		if (c == '<') {
			this.flush();
			this._state = XmlStreamParser.kElement;
		} else {
			this._value += c;
		}
		break;
	case XmlStreamParser.kElement:
	case XmlStreamParser.kEndElement:
		switch (c) {
		case '>':
			this.finishElement();
			break;
		case '/':
			if (!this._value) {
				this._state = XmlStreamParser.kEndElement;
			} else if (!this._slash) {
				this._slash = true;
			} else {
				this._value += c;
			}
			break;
		case '?':
			if (!this._value) {
				this._decl = true;
			} else {
				this._value += '?';
			}
			break;
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			this._state = XmlStreamParser.kAttributeName;
			break;
		default:
			if (this._slash) {
				this._slash = false;
				this._value += '/';
			}
			this._value += c;
			break;
		}
		break;
	case XmlStreamParser.kAttributeName:
		switch (c) {
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			if (this._attributeName) {
				this._state = XmlStreamParser.kAttributeValue;
			}
			break;
		case '/':
			if (!this._slash) {
				this._slash = true;
			} else {
				this._value += '/';
			}
			break;
		case '=':
			this._state = XmlStreamParser.kAttributeValue;
			break;
		case '>':
			if (this._attributeName) {
				this._attributes[this._attributeName] = null;
			}
			this._state = XmlStreamParser.kElement;
			this.finishElement();
			break;
		default:
			this._attributeName += c;
			break;
		}
		break;
	case XmlStreamParser.kAttributeValue:
		switch (c) {
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			if (this._attributeValue) {
				this._state = XmlStreamParser.kAttributeName;
			}
			break;
		case '"':
		case "'":
			if (!this._attributeValue && !this._attributeQuote) {
				this._attributeQuote = c;
			} else if (this._attributeQuote == c) {
				this._attributes[this._attributeName] = this._attributeValue;
				this._attributeName = "";
				this._attributeValue = "";
				this._attributeQuote = "";
				this._state = XmlStreamParser.kAttributeName;
			} else {
				this._attributeValue += c;
			}
			break;
		case '>':
			this.finishElement();
			break;
		default:
			this._attributeValue += c;
			break;
		}
		break;
	}
}

XmlStreamParser.prototype.finishElement = function() {
	if (this._decl) {
		this.reset();
	} else {
		var value = this._value;
		var slash = this._slash;
		this.flush();
		if (slash) {
			this._state = XmlStreamParser.kEndElement;
			this._value = value;
			this.flush();
		}
	}
	this._state = XmlStreamParser.kText;
}

XmlStreamParser.prototype.emit = function(node) {
	this._parsed.push(node);
}

function XmlStanzaParser(depth) {
	this._depth = depth || 0;
	this._parsed = [];
	this._stack = [];
	this._stream = new XmlStreamParser();
	return this;
}

XmlStanzaParser.prototype.reset = function() {
	this._parsed = [];
	this._stack = [];
	this._stream.reset();
}

XmlStanzaParser.prototype.emit = function(stanza) {
	this._parsed.push(stanza);
}

XmlStanzaParser.prototype.parse = function(data) {
	this._parsed = [];
	var nodes = this._stream.parse(data);
	for (var i = 0; i < nodes.length; i++) {
		this.parseNode(nodes[i]);
	}
	return this._parsed;
}

XmlStanzaParser.prototype.parseNode = function(node) {
	switch (node.type) {
	case XmlStreamParser.kElement:
		this._stack.push({name: node.value, attributes: node.attributes, children: [], text: ""});
		break;
	case XmlStreamParser.kEndElement:
		if (this._stack.length == 1 + this._depth) {
			this.emit(this._stack.pop());
		} else {
			var last = this._stack.pop();
			this._stack[this._stack.length - 1].children.push(last);
		}
		break;
	case XmlStreamParser.kText:
		if (this._stack) {
			this._stack[this._stack.length - 1].text += node.value;
		}
		break;
	}
}

exports.XmlStanzaParser = XmlStanzaParser;