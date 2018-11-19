
#include <unistd.h>

#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>

#include "main.hh"

ChatDialog::ChatDialog() {
    setWindowTitle("P2Papp");

    socket = new NetSocket();
	if (!socket->bind()) {
		exit(1);
	} else {
		portNum = socket->port;
		originName = QVariant(portNum).toString();
        qDebug() << "origin name: " << originName;
	}
    seqNo = 1;

    // Read-only text box where we display messages from everyone.
    // This widget expands both horizontally and vertically.
    textview = new QTextEdit(this);
    textview->setReadOnly(true);

    // Small text-entry box the user can enter messages.
    // This widget normally expands only horizontally,
    // leaving extra vertical space for the textview widget.
    //
    // You might change this into a read/write QTextEdit,
    // so that the user can easily enter multi-line messages.
    textline = new QLineEdit(this);

    // Lay out the widgets to appear in the main window.
    // For Qt widget and layout concepts see:
    // http://doc.qt.nokia.com/4.7-snapshot/widgets-and-layouts.html
    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(textview);
    layout->addWidget(textline);
    setLayout(layout);

    // Register a callback on the textline's returnPressed signal
    // so that we can send the message entered by the user.
    connect(textline, SIGNAL(returnPressed()),
            this, SLOT(gotReturnPressed()));
    connect(socket, SIGNAL(readyRead()),
			this, SLOT(receiveDatagrams()));
}

void ChatDialog::gotReturnPressed() {
    // Initially, just echo the string locally.
    // Insert some networking code here...
    qDebug() << "FIX: send message to other peers: " << textline->text();
    textview->append(textline->text());

    // process the message vis socket
    QString message = textline->text();
    if (messageDict.contains(originName)) {
    	QStringList myMessage = messageDict[originName];
    	myMessage.append(message);
    	messageDict[originName] = myMessage;
    } else {
    	QStringList myMessage = (QStringList() << "");
    	myMessage.append(message);
    	messageDict[originName] = myMessage;
    }
	sendRumorMessage(originName, quint32(seqNo));
	seqNo += 1;

    // Clear the textline to get ready for the next input message.
    textline->clear();
}

void ChatDialog::receiveDatagrams() {
    qDebug() << "receive datagram";
	while (socket->hasPendingDatagrams()) {
		QByteArray datagram;
		datagram.resize(socket->pendingDatagramSize());
        QHostAddress senderHost;
        quint16 senderPort;
		if(socket->readDatagram(
		        datagram.data(),
		        datagram.size(),
		        &senderHost,
		        &senderPort) != -1) {
			deserializeMessage(datagram, senderHost, senderPort);
		}
	}
}

quint16 ChatDialog::findPort() {
	if (portNum == socket->myPortMax) {
		return portNum - 1;
	} else if (portNum == socket->myPortMin) {
		return portNum + 1;
	} else {
		if (qrand() % 2 == 0) {
			return portNum - 1;
		} else {
			return portNum + 1;
		}
	}
}

void ChatDialog::serializeMessage(
        QVariantMap message, QHostAddress senderHost, quint16 senderPort) {
    // To serialize a message you’ll need to construct a QVariantMap describing
    // the message
    qDebug() << "serialize Message";
	QByteArray datagram;
	QDataStream outStream(&datagram, QIODevice::ReadWrite); 
	outStream << message;

	quint16 destPort = findPort();
    qDebug() << "Sending message to port: " << destPort;

	socket->writeDatagram(
	        datagram.data(),
	        datagram.size(),
	        QHostAddress::LocalHost,
	        destPort);
	setTimeout();
}

void ChatDialog::deserializeMessage(
        QByteArray datagram, QHostAddress senderHost, quint16 senderPort) {
    // using QDataStream, and handle the message as appropriate
    // containing a ChatText key with a value of type QString
    qDebug() << "deserialize Message";
    QVariantMap message;
    QDataStream inStream(&datagram, QIODevice::ReadOnly);
    inStream >> message;
    if (message.contains("Want")) {
        receiveStatusMessage(message, senderHost, senderPort);
    } else {
        receiveRumorMessage(message, senderHost, senderPort);
    }
}

void ChatDialog::receiveRumorMessage(
        QVariantMap message, QHostAddress senderHost, quint16 senderPort) {
    // <”ChatText”,”Hi”> <”Origin”,”tiger”> <”SeqNo”,23>
    qDebug() << "receive RumorMessage";
    if (!message.contains("ChatText") ||
        !message.contains("Origin") ||
        !message.contains("SeqNo")) {
        // Invalid message.
        qDebug() << "WARNING: Received invalid rumor message!";
        return;
    }

    QString messageChatText = message["ChatText"].toString();
    QString messageOrigin = message["Origin"].toString();
    quint32 messageSeqNo = message["SeqNo"].toUInt();

    // if ((messageDict.contains(messageOrigin) &&
    //     messageDict[messageOrigin].length() != messageSeqNo) ||
    //     (!messageDict.contains(messageOrigin) &&
    //     messageSeqNo != 1)) {
    // 	qDebug() << "skip message";
    //     // skip duplicate and disorder.
    //     return;
    // } else {
    textview->append(messageOrigin + ": ");
    textview->append(messageChatText);
    if (!messageDict.contains(messageOrigin)) {
        messageDict[messageOrigin] = (QStringList() << ""); // skip 0 index.
    }

    quint32 last_seqno = messageDict[messageOrigin].size();

    if (messageSeqNo == last_seqno) {
        textview->append(messageOrigin + ": ");
        textview->append(messageChatText);
    	messageDict[messageOrigin].append(messageChatText);
    	sendStatusMessage(messageOrigin, quint32(last_seqno + 1));
    } else if (messageSeqNo < last_seqno) {
    	sendRumorMessage(messageOrigin, last_seqno - 1);
    } else {
    	sendStatusMessage(messageOrigin, last_seqno);
    }
    messageDict[messageOrigin].append(messageChatText);
    sendRumorMessage(messageOrigin, messageSeqNo);
    // }
}

void ChatDialog::receiveStatusMessage(
        QVariantMap message, QHostAddress senderHost, quint16 senderPort) {
    // <"Want",<"tiger",4>> 4 is the message don't have
    qDebug() << "receive StatusMessage";
    QVariantMap statusMap = qvariant_cast<QVariantMap>(message["Want"]);
    QList<QString> messageOriginList = statusMap.keys();
    for (QString origin: statusMap.keys()) {
    	quint32 seqno = statusMap[origin].value<quint32>();
    	if (messageDict.contains(origin)) {
    		quint32 last_seqno = messageDict[origin].size();
    		if (seqno > last_seqno) {
    		    // find this user need to update the message from origin
    			sendStatusMessage(origin, quint32(last_seqno + 1));
    		} else if (seqno < last_seqno) {
    		    // find sender need to update the message from origin
    			sendRumorMessage(origin, quint32(seqno + 1));
    		}

    	} else {
			sendStatusMessage(origin, quint32(1));
		}
    }
}

void ChatDialog::sendRumorMessage(
        QString origin,
        quint32 seqno,
        QHostAddress senderHost,
        quint16 senderPort) {
    qDebug() << "sending RumorMessage from: " << origin << seqno;
	QVariantMap message;
	if (messageDict[origin].size() > seqno) {
		message.insert(QString("ChatText"), messageDict[origin].at(seqno));
		message.insert(QString("Origin"), QString(origin));
		message.insert(QString("SeqNo"), quint32(seqno));
	
		serializeMessage(message);
	}
}

void ChatDialog::sendStatusMessage(
        QString origin,
        quint32 seqno,
        QHostAddress senderHost,
        quint16 senderPort) {
    qDebug() << "sending StatusMessage: " << origin << seqno;
	QVariantMap message;
	QVariantMap inner;

	inner.insert(origin, seqno);
	message.insert(QString("Want"), inner);
	serializeMessage(message);
}

QVariantMap ChatDialog::buildStatusMessage() {
    QVariantMap statusMessage;
    QVariantMap statusVector;
    for (QString origin: messageDict.keys()) {
        statusVector[origin] = messageDict[origin].length();
    }
    statusMessage["Want"] = statusVector;
    return statusMessage;
}

void ChatDialog::setTimeout() {
    // Use QTimer
}

void ChatDialog::vectorClock() {
    // Use QTimer
}

NetSocket::NetSocket() {
    // Pick a range of four UDP ports to try to allocate by default,
    // computed based on my Unix user ID.
    // This makes it trivial for up to four P2Papp instances per user
    // to find each other on the same host,
    // barring UDP port conflicts with other applications
    // (which are quite possible).
    // We use the range from 32768 to 49151 for this purpose.
    myPortMin = 32768 + (getuid() % 4096) * 4;
    myPortMax = myPortMin + 3;
}

bool NetSocket::bind() {
    // Try to bind to each of the range myPortMin..myPortMax in turn.
    for (int p = myPortMin; p <= myPortMax; p++) {
        if (QUdpSocket::bind(p)) {
            qDebug() << "bound to UDP port " << p;
            port = p;
            return true;
        }
    }

    qDebug() << "Oops, no ports in my default range " << myPortMin
             << "-" << myPortMax << " available";
    return false;
}

int main(int argc, char **argv) {
    // Initialize Qt toolkit
    QApplication app(argc, argv);

    // Create an initial chat dialog window
    ChatDialog dialog;
    dialog.show();

    // Enter the Qt main loop; everything else is event driven
    return app.exec();
}
