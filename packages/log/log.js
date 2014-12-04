var kMessageLimit = 256;
var gMessages = [];
var gMessageIndex = 0;
var gWaiting = [];

function append(line) {
	gMessages.push(line);
	gMessageIndex++;
	if (gMessages.length > kMessageLimit) {
		gMessages.splice(0, gMessages.length - kMessageLimit);
	}

	for (var i in gWaiting) {
		gWaiting[i]({messages: [line], next: gMessageIndex});
	}
	gWaiting.length = 0;
}

function getMessages(start) {
	if (!start || start < gMessageIndex) {
		var messages = gMessages;
		if (start) {
			messages = messages.slice(Math.max(messages.length - gMessageIndex + start, 0));
		}
		return {messages: messages, next: gMessageIndex};
	} else {
		var promise = new Promise(function(resolve, reject) {
			gWaiting.push(resolve);
		});
		return promise;
	}
}

exports = {
	append: append,
	getMessages: getMessages,
};
